#include "NetworkManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

using namespace XYChat::Protocol;

NetworkManager::NetworkManager(QObject *parent) :
    QObject(parent),
    m_tcpSocket(new QTcpSocket(this)),
    m_heartbeatTimer(new QTimer(this)),
    m_reconnectTimer(new QTimer(this))
{
    m_heartbeatTimer->setInterval(30000);
    m_reconnectTimer->setInterval(3000);
    m_reconnectTimer->setSingleShot(true);

    connect(m_tcpSocket, &QTcpSocket::connected, this, &NetworkManager::onConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &NetworkManager::onDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &NetworkManager::onSocketError);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &NetworkManager::sendHeartbeat);
    connect(m_reconnectTimer, &QTimer::timeout, this, &NetworkManager::connectToServer);
}

// ── 登录 ─────────────────────────────────────────────────────────────────────
void NetworkManager::login(const QString &username, const QString &encryptedPassword)
{
    m_pendingUsername = username;
    m_pendingEncryptedPassword = encryptedPassword;
    m_loginQueued = true;
    m_reconnectEnabled = true;

    if (m_state == ConnectionState::Connected || m_state == ConnectionState::Authenticated) {
        sendLoginRequest();
        return;
    }

    if (m_state == ConnectionState::Disconnected) {
        connectToServer();
    }
}

// ── 注册 ─────────────────────────────────────────────────────────────────────
void NetworkManager::registerAccount(const QString &username, const QString &password,
                                     const QString &email, const QString &phone)
{
    m_pendingUsername = username;
    m_pendingRegisterPassword = password;
    m_pendingEmail = email;
    m_pendingPhone = phone;
    m_registerQueued = true;

    if (m_state == ConnectionState::Connected || m_state == ConnectionState::Authenticated) {
        sendRegisterRequest();
        return;
    }

    if (m_state == ConnectionState::Disconnected) {
        connectToServer();
    }
}

// ── 登出 ─────────────────────────────────────────────────────────────────────
void NetworkManager::logout()
{
    if (m_state != ConnectionState::Authenticated) {
        return;
    }

    QJsonObject json;
    json["type"] = "logout";

    Packet packet;
    packet.messageType = MessageType::LogoutRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    sendPacket(packet);
}

// ── Token 续期 ───────────────────────────────────────────────────────────────
void NetworkManager::renewToken()
{
    if (m_state != ConnectionState::Authenticated || m_sessionToken.isEmpty()) {
        return;
    }

    QJsonObject json;
    json["type"] = "token_renew";
    json["token"] = m_sessionToken;

    Packet packet;
    packet.messageType = MessageType::TokenRenewRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    sendPacket(packet);
}

// ── 连接回调 ─────────────────────────────────────────────────────────────────
void NetworkManager::onConnected()
{
    setState(ConnectionState::Connected);
    m_heartbeatTimer->start();

    if (m_loginQueued) {
        sendLoginRequest();
    } else if (m_registerQueued) {
        sendRegisterRequest();
    }
}

void NetworkManager::onDisconnected()
{
    const bool shouldRelogin = m_reconnectEnabled && !m_pendingUsername.isEmpty();

    m_heartbeatTimer->stop();
    m_codec.reset();
    m_pendingLoginRequestId = 0;
    setState(ConnectionState::Disconnected);

    if (shouldRelogin) {
        m_loginQueued = true;
        m_reconnectTimer->start();
    }
}

void NetworkManager::onReadyRead()
{
    m_codec.appendData(m_tcpSocket->readAll());

    while (true) {
        Packet packet;
        QString errorMessage;
        const PacketCodec::DecodeStatus status = m_codec.nextPacket(packet, &errorMessage);
        if (status == PacketCodec::DecodeStatus::NeedMoreData) {
            return;
        }
        if (status == PacketCodec::DecodeStatus::InvalidData) {
            emit loginFailed(errorMessage);
            m_tcpSocket->disconnectFromHost();
            return;
        }

        handlePacket(packet);
    }
}

void NetworkManager::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);

    if (m_state == ConnectionState::Connecting || m_state == ConnectionState::LoggingIn) {
        emit loginFailed(m_tcpSocket->errorString());
    }
}

void NetworkManager::sendHeartbeat()
{
    if (m_state == ConnectionState::Disconnected || m_state == ConnectionState::Connecting) {
        return;
    }

    Packet packet;
    packet.messageType = MessageType::Ping;
    packet.requestId = nextRequestId();
    sendPacket(packet);
}

void NetworkManager::connectToServer()
{
    if (m_state != ConnectionState::Disconnected) {
        return;
    }

    setState(ConnectionState::Connecting);
    m_tcpSocket->connectToHost("127.0.0.1", 12345);
}

// ── 发送登录请求 ─────────────────────────────────────────────────────────────
void NetworkManager::sendLoginRequest()
{
    QJsonObject json;
    json["type"] = "login";
    json["username"] = m_pendingUsername;
    json["password"] = m_pendingEncryptedPassword;
    json["clientVersion"] = "0.2.0";
    json["platform"] = QSysInfo::productType();
    json["deviceId"] = QString::fromLatin1(QSysInfo::machineUniqueId().toHex());

    Packet packet;
    packet.messageType = MessageType::LoginRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);

    m_pendingLoginRequestId = packet.requestId;
    setState(ConnectionState::LoggingIn);
    sendPacket(packet);
}

// ── 发送注册请求 ─────────────────────────────────────────────────────────────
void NetworkManager::sendRegisterRequest()
{
    QJsonObject json;
    json["type"] = "register";
    json["username"] = m_pendingUsername;
    json["password"] = m_pendingRegisterPassword;
    json["email"] = m_pendingEmail;
    json["phone"] = m_pendingPhone;

    Packet packet;
    packet.messageType = MessageType::RegisterRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);

    m_pendingRegisterRequestId = packet.requestId;
    setState(ConnectionState::LoggingIn);
    sendPacket(packet);
}

// ── 包分发 ───────────────────────────────────────────────────────────────────
void NetworkManager::handlePacket(const Packet &packet)
{
    switch (packet.messageType) {
    case MessageType::LoginResponse:
        handleLoginResponse(packet);
        break;
    case MessageType::RegisterResponse:
        handleRegisterResponse(packet);
        break;
    case MessageType::LogoutResponse:
        handleLogoutResponse(packet);
        break;
    case MessageType::TokenRenewResponse:
        handleTokenRenewResponse(packet);
        break;
    case MessageType::Ping: {
        Packet pong;
        pong.messageType = MessageType::Pong;
        pong.requestId = packet.requestId;
        sendPacket(pong);
        break;
    }
    default:
        break;
    }
}

// ── 登录响应 ─────────────────────────────────────────────────────────────────
void NetworkManager::handleLoginResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingLoginRequestId) {
        return;
    }

    const QJsonDocument responseDoc = QJsonDocument::fromJson(packet.payload);
    const QJsonObject response = responseDoc.object();
    const int code = response.value("code").toInt(static_cast<int>(ErrorCode::InternalError));

    m_pendingLoginRequestId = 0;
    if (code == static_cast<int>(ErrorCode::Ok)) {
        const QJsonObject data = response.value("data").toObject();
        m_sessionToken = data.value("token").toString();
        m_userId = data.value("userId").toVariant().toLongLong();
        m_username = data.value("username").toString();
        m_loginQueued = false;
        setState(ConnectionState::Authenticated);
        emit loginSuccessful();
        return;
    }

    setState(ConnectionState::Connected);
    const QString message = response.value("message").toString("Unknown error");
    emit loginFailed(message);
}

// ── 注册响应 ─────────────────────────────────────────────────────────────────
void NetworkManager::handleRegisterResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingRegisterRequestId) {
        return;
    }

    const QJsonDocument responseDoc = QJsonDocument::fromJson(packet.payload);
    const QJsonObject response = responseDoc.object();
    const int code = response.value("code").toInt(static_cast<int>(ErrorCode::InternalError));

    m_pendingRegisterRequestId = 0;
    m_registerQueued = false;

    if (code == static_cast<int>(ErrorCode::Ok)) {
        setState(ConnectionState::Connected);
        emit registerSuccessful();
        return;
    }

    setState(ConnectionState::Connected);
    const QString message = response.value("message").toString("Unknown error");
    emit registerFailed(message);
}

// ── 登出响应 ─────────────────────────────────────────────────────────────────
void NetworkManager::handleLogoutResponse(const Packet &packet)
{
    Q_UNUSED(packet);
    resetAuthState();
    m_reconnectEnabled = false;
    setState(ConnectionState::Connected);
    emit logoutFinished();
}

// ── Token 续期响应 ───────────────────────────────────────────────────────────
void NetworkManager::handleTokenRenewResponse(const Packet &packet)
{
    const QJsonDocument responseDoc = QJsonDocument::fromJson(packet.payload);
    const QJsonObject response = responseDoc.object();
    const int code = response.value("code").toInt(static_cast<int>(ErrorCode::InternalError));

    if (code == static_cast<int>(ErrorCode::Ok)) {
        const QJsonObject data = response.value("data").toObject();
        m_sessionToken = data.value("token").toString();
        qDebug() << "[NetMgr] Token renewed";
    }
}

// ── 工具 ─────────────────────────────────────────────────────────────────────
void NetworkManager::sendPacket(const Packet &packet)
{
    const QByteArray encoded = PacketCodec::encode(packet);
    if (encoded.isEmpty()) {
        emit loginFailed("Failed to encode request");
        return;
    }

    m_tcpSocket->write(encoded);
}

quint64 NetworkManager::nextRequestId()
{
    return m_nextRequestId++;
}

void NetworkManager::setState(ConnectionState state)
{
    if (m_state == state) {
        return;
    }

    m_state = state;
    emit connectionStateChanged(m_state);
}

void NetworkManager::resetAuthState()
{
    m_sessionToken.clear();
    m_userId = 0;
    m_username.clear();
    m_pendingUsername.clear();
    m_pendingEncryptedPassword.clear();
}
