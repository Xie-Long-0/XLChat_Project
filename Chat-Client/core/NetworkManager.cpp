#include "NetworkManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QDateTime>
#include <QUuid>
#include <QSslConfiguration>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QCoreApplication>
#include <QStandardPaths>

#include "EncryptionManager.h"
#include "TlsHelper.h"
#include "SecureMemory.h"

using namespace XYChat::Protocol;

NetworkManager::NetworkManager(QObject *parent) :
    QObject(parent),
    m_sslSocket(new QSslSocket(this)),
    m_heartbeatTimer(new QTimer(this)),
    m_reconnectTimer(new QTimer(this))
{
    m_heartbeatTimer->setInterval(30000);
    m_reconnectTimer->setInterval(3000);
    m_reconnectTimer->setSingleShot(true);

    connect(m_sslSocket, &QSslSocket::connected, this, &NetworkManager::onConnected);
    connect(m_sslSocket, &QSslSocket::disconnected, this, &NetworkManager::onDisconnected);
    connect(m_sslSocket, &QSslSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(m_sslSocket, &QSslSocket::errorOccurred, this, &NetworkManager::onSocketError);
    connect(m_sslSocket, &QSslSocket::sslErrors, this, &NetworkManager::onSslErrors);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &NetworkManager::sendHeartbeat);
    connect(m_reconnectTimer, &QTimer::timeout, this, &NetworkManager::connectToServer);

    // M5: 初始化 TLS
    initTls();
}

// ── 登录 ─────────────────────────────────────────────────────────────────────
void NetworkManager::login(const QString &username, const QString &password)
{
    m_pendingUsername = username;
    m_pendingPassword = password;
    m_loginQueued = true;
    m_registerQueued = false;
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
    m_loginQueued = false;
    m_reconnectEnabled = false;

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
    addReplayProtection(json);

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
    addReplayProtection(json);

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
    // M5.5: 在途发送请求随连接丢失，清除映射，
    // 重新登录后由 outbox 以相同幂等键重发（服务端去重）
    m_pendingSendByRequestId.clear();
    setState(ConnectionState::Disconnected);

    if (shouldRelogin) {
        m_loginQueued = true;
        m_reconnectTimer->start();
    }
}

void NetworkManager::onReadyRead()
{
    m_codec.appendData(m_sslSocket->readAll());

    while (true) {
        Packet packet;
        QString errorMessage;
        const PacketCodec::DecodeStatus status = m_codec.nextPacket(packet, &errorMessage);
        if (status == PacketCodec::DecodeStatus::NeedMoreData) {
            return;
        }
        if (status == PacketCodec::DecodeStatus::InvalidData) {
            emit loginFailed(errorMessage);
            m_sslSocket->disconnectFromHost();
            return;
        }

        handlePacket(packet);
    }
}

void NetworkManager::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);

    if (m_state == ConnectionState::Connecting || m_state == ConnectionState::LoggingIn) {
        const QString err = m_sslSocket->errorString();
        if (m_registerQueued) {
            m_registerQueued = false;
            emit registerFailed(err);
        } else {
            m_loginQueued = false;
            emit loginFailed(err);
        }
        setState(ConnectionState::Disconnected);
    }
}

// ── M5: SSL 错误处理 ───────────────────────────────────────────────────────
void NetworkManager::onSslErrors(const QList<QSslError> &errors)
{
    // 证书错误时明确拒绝连接
    QStringList errorStrings;
    for (const auto &err : errors) {
        errorStrings << err.errorString();
    }
    const QString errMsg = "TLS certificate error: " + errorStrings.join("; ");
    qWarning() << "[NetMgr]" << errMsg;

    if (m_registerQueued) {
        m_registerQueued = false;
        emit registerFailed(errMsg);
    } else if (m_loginQueued) {
        m_loginQueued = false;
        emit loginFailed(errMsg);
    }

    m_sslSocket->disconnectFromHost();
    setState(ConnectionState::Disconnected);
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

    // M5.5: fail-closed —— TLS 不可用时拒绝连接，
    // 除非显式设置环境变量 XYCHAT_ALLOW_PLAINTEXT=1（仅限开发）
    if (!m_tlsEnabled) {
        if (qEnvironmentVariable("XYCHAT_ALLOW_PLAINTEXT") == "1") {
            qWarning() << "[NetMgr] Connecting in PLAINTEXT development mode."
                       << "Do not use in production.";
        } else {
            const QString err = QStringLiteral(
                "TLS unavailable: CA certificate not found. "
                "Refusing plaintext connection (set XYCHAT_ALLOW_PLAINTEXT=1 for development only).");
            qCritical() << "[NetMgr]" << err;
            m_reconnectEnabled = false;
            if (m_loginQueued) {
                m_loginQueued = false;
                emit loginFailed(err);
            } else if (m_registerQueued) {
                m_registerQueued = false;
                emit registerFailed(err);
            }
            return;
        }
    }

    setState(ConnectionState::Connecting);
    if (m_tlsEnabled) {
        m_sslSocket->connectToHostEncrypted("127.0.0.1", 12345);
    } else {
        m_sslSocket->connectToHost("127.0.0.1", 12345);
    }
}

// ── M5: TLS 初始化 ─────────────────────────────────────────────────────────
void NetworkManager::initTls()
{
    using namespace XYChat::Security;

    // 查找 CA 证书：优先可执行文件同级 certs 目录，其次 AppData
    QString caCertPath;
    const QStringList searchDirs = {
        QCoreApplication::applicationDirPath() + "/certs",
        QCoreApplication::applicationDirPath() + "/../certs",
        TlsHelper::defaultCertDir()
    };

    for (const auto &dir : searchDirs) {
        const QString candidate = dir + "/ca.crt";
        if (QFile::exists(candidate)) {
            caCertPath = candidate;
            break;
        }
    }

    if (caCertPath.isEmpty()) {
        qWarning() << "[NetMgr] CA certificate not found, TLS disabled (fail-closed on connect)";
        m_tlsUnavailable = true;
        return;
    }

    TlsHelper::TlsConfig config = TlsHelper::loadClientConfig(caCertPath);
    if (!config.valid) {
        qWarning() << "[NetMgr] Failed to load CA certificate, TLS disabled (fail-closed on connect)";
        m_tlsUnavailable = true;
        return;
    }

    // 配置 SSL：添加 CA 证书用于验证服务端
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.addCaCertificate(config.caCertificate);
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyPeer);
    m_sslSocket->setSslConfiguration(sslConfig);
    m_tlsEnabled = true;

    qInfo() << "[NetMgr] TLS enabled, CA:" << caCertPath;
}

// ── 发送登录请求 ─────────────────────────────────────────────────────────────
void NetworkManager::sendLoginRequest()
{
    QJsonObject json;
    json["type"] = "login";
    json["username"] = m_pendingUsername;
    json["password"] = m_pendingPassword;
    json["clientVersion"] = "0.2.0";
    json["platform"] = QSysInfo::productType();
    json["deviceId"] = QString::fromLatin1(QSysInfo::machineUniqueId().toHex());
    addReplayProtection(json);

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
    addReplayProtection(json);

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
    // M3 响应分发
    case MessageType::SearchUsersResponse:
        handleSearchUsersResponse(packet);
        break;
    case MessageType::AddContactResponse:
        handleAddContactResponse(packet);
        break;
    case MessageType::GetContactsResponse:
        handleGetContactsResponse(packet);
        break;
    case MessageType::GetConversationsResponse:
        handleGetConversationsResponse(packet);
        break;
    case MessageType::SendMessageResponse:
        handleSendMessageResponse(packet);
        break;
    case MessageType::AckMessageResponse:
        handleAckMessageResponse(packet);
        break;
    case MessageType::SyncMessagesResponse:
        handleSyncMessagesResponse(packet);
        break;
    case MessageType::NewMessageNotification:
        handleNewMessageNotification(packet);
        break;
    // M5.5
    case MessageType::MessageStatusUpdate:
        handleMessageStatusUpdate(packet);
        break;
    case MessageType::SyncEventsResponse:
        handleSyncEventsResponse(packet);
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
        emit sessionChanged();
        emit loginSuccessful();
        // M5.5: 登录成功后重发 outbox 中未确认的消息（幂等键保证不重复）
        flushOutbox();
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
        emit sessionChanged();
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

    m_sslSocket->write(encoded);
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
    // M5: 安全清除敏感数据
    XYChat::Security::SecureMemory::wipe(m_sessionToken);
    XYChat::Security::SecureMemory::wipe(m_pendingPassword);
    XYChat::Security::SecureMemory::wipe(m_pendingRegisterPassword);
    m_userId = 0;
    m_username.clear();
    m_pendingUsername.clear();
    emit sessionChanged();
}

// ── M5: 重放保护 ───────────────────────────────────────────────────────────
void NetworkManager::addReplayProtection(QJsonObject &json)
{
    json["timestamp"] = QDateTime::currentSecsSinceEpoch();
    json["nonce"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// ── M3: 用户搜索 ───────────────────────────────────────────────────────────
void NetworkManager::searchUsers(const QString &query)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "search_users";
    json["query"] = query;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::SearchUsersRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingSearchRequestId = packet.requestId;
    sendPacket(packet);
}

// ── M3: 添加联系人 ───────────────────────────────────────────────────────────
void NetworkManager::addContact(qint64 userId)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "add_contact";
    json["userId"] = userId;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::AddContactRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingAddContactRequestId = packet.requestId;
    sendPacket(packet);
}

// ── M3: 获取联系人列表 ─────────────────────────────────────────────────────
void NetworkManager::getContacts()
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "get_contacts";
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::GetContactsRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingGetContactsRequestId = packet.requestId;
    sendPacket(packet);
}

// ── M3: 获取会话列表 ─────────────────────────────────────────────────────
void NetworkManager::getConversations()
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "get_conversations";
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::GetConversationsRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingGetConversationsRequestId = packet.requestId;
    sendPacket(packet);
}

// ── M3: 发送消息 ───────────────────────────────────────────────────────────
void NetworkManager::sendMessage(qint64 toUserId, const QString &content)
{
    if (m_state != ConnectionState::Authenticated) {
        // M5.5: 未认证时进入 outbox，登录成功后自动重发
        m_outbox.append({QUuid::createUuid().toString(QUuid::WithoutBraces), toUserId, content});
        return;
    }

    // M5.5: 客户端生成幂等键，重试/重连重发不会产生重复消息
    const QString clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_outbox.append({clientMessageId, toUserId, content});
    flushOutbox();
}

// M5.5: 将 outbox 中未确认的消息逐条发送（同一 clientMessageId 只保留一份）
void NetworkManager::flushOutbox()
{
    if (m_state != ConnectionState::Authenticated) {
        return;
    }

    // 已在途的 clientMessageId 不重复发
    QSet<QString> inFlight;
    for (auto it = m_pendingSendByRequestId.constBegin();
         it != m_pendingSendByRequestId.constEnd(); ++it) {
        inFlight.insert(it.value());
    }

    for (const OutboxItem &item : std::as_const(m_outbox)) {
        if (inFlight.contains(item.clientMessageId)) {
            continue;
        }

        QJsonObject json;
        json["type"] = "send_message";
        json["toUserId"] = item.toUserId;
        json["content"] = item.content;
        json["contentType"] = "text";
        json["clientMessageId"] = item.clientMessageId;
        addReplayProtection(json);

        Packet packet;
        packet.messageType = MessageType::SendMessageRequest;
        packet.requestId = nextRequestId();
        packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
        m_pendingSendByRequestId.insert(packet.requestId, item.clientMessageId);
        sendPacket(packet);
    }
}

// ── M3: 确认消息 ───────────────────────────────────────────────────────────
void NetworkManager::ackMessage(qint64 messageId, const QString &status)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "ack_message";
    json["messageId"] = messageId;
    json["status"] = status;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::AckMessageRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingAckMessageRequestId = packet.requestId;
    sendPacket(packet);
}

// ── M3: 同步消息 ───────────────────────────────────────────────────────────
void NetworkManager::syncMessages(qint64 conversationId, qint64 afterId, int limit)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "sync_messages";
    json["conversationId"] = conversationId;
    json["afterId"] = afterId;
    json["limit"] = limit;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::SyncMessagesRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingSyncMessagesRequestId = packet.requestId;
    sendPacket(packet);
}

// ── M3: 响应处理 ───────────────────────────────────────────────────────────
void NetworkManager::handleSearchUsersResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingSearchRequestId) return;
    m_pendingSearchRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() == static_cast<int>(ErrorCode::Ok)) {
        emit searchUsersResult(response.value("data").toObject().value("users").toArray());
    }
}

void NetworkManager::handleAddContactResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingAddContactRequestId) return;
    m_pendingAddContactRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() != static_cast<int>(ErrorCode::Ok)) {
        emit messageSendFailed(response.value("message").toString("Failed to add contact"));
    }
}

void NetworkManager::handleGetContactsResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingGetContactsRequestId) return;
    m_pendingGetContactsRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() == static_cast<int>(ErrorCode::Ok)) {
        emit contactsResult(response.value("data").toObject().value("contacts").toArray());
    }
}

void NetworkManager::handleGetConversationsResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingGetConversationsRequestId) return;
    m_pendingGetConversationsRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() == static_cast<int>(ErrorCode::Ok)) {
        emit conversationsResult(response.value("data").toObject().value("conversations").toArray());
    }
}

void NetworkManager::handleSendMessageResponse(const Packet &packet)
{
    auto it = m_pendingSendByRequestId.constFind(packet.requestId);
    if (it == m_pendingSendByRequestId.constEnd()) return;
    const QString clientMessageId = it.value();
    m_pendingSendByRequestId.erase(it);

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    const int code = response.value("code").toInt();
    if (code == static_cast<int>(ErrorCode::Ok)) {
        const QJsonObject data = response.value("data").toObject();
        // 确认后从 outbox 移除（以服务端回传的幂等键为准）
        const QString ackedId = data.value("clientMessageId").toString(clientMessageId);
        for (int i = 0; i < m_outbox.size(); ++i) {
            if (m_outbox.at(i).clientMessageId == ackedId) {
                m_outbox.removeAt(i);
                break;
            }
        }
        emit messageSent(data.value("messageId").toVariant().toLongLong(),
                         data.value("conversationId").toVariant().toLongLong());
    } else {
        emit messageSendFailed(response.value("message").toString("Send failed"));
    }
}

void NetworkManager::handleAckMessageResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingAckMessageRequestId) return;
    m_pendingAckMessageRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() == static_cast<int>(ErrorCode::Ok)) {
        emit messageAcked(response.value("data").toObject().value("messageId").toVariant().toLongLong());
    }
}

void NetworkManager::handleSyncMessagesResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingSyncMessagesRequestId) return;
    m_pendingSyncMessagesRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() == static_cast<int>(ErrorCode::Ok)) {
        const QJsonObject data = response.value("data").toObject();
        emit messagesSynced(
            data.value("conversationId").toVariant().toLongLong(),
            data.value("messages").toArray(),
            data.value("hasMore").toBool());
    }
}

void NetworkManager::handleNewMessageNotification(const Packet &packet)
{
    const QJsonObject msg = QJsonDocument::fromJson(packet.payload).object();
    emit newMessageReceived(msg);

    // 自动发送已送达确认
    const qint64 msgId = msg.value("messageId").toVariant().toLongLong();
    if (msgId > 0) {
        ackMessage(msgId, "delivered");
    }
}

// ── M5.5: 消息状态更新推送 ─────────────────────────────────────
void NetworkManager::handleMessageStatusUpdate(const Packet &packet)
{
    const QJsonObject msg = QJsonDocument::fromJson(packet.payload).object();
    const qint64 msgId = msg.value("messageId").toVariant().toLongLong();
    const QString status = msg.value("status").toString();
    if (msgId > 0 && !status.isEmpty()) {
        emit messageStatusChanged(msgId, status);
    }
}

// ── M5.5: 账号级增量同步 ───────────────────────────────────────
void NetworkManager::syncEvents(qint64 afterSeq, int limit)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "sync_events";
    json["afterSeq"] = afterSeq;
    json["limit"] = limit;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::SyncEventsRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingSyncEventsRequestId = packet.requestId;
    sendPacket(packet);
}

void NetworkManager::handleSyncEventsResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingSyncEventsRequestId) return;
    m_pendingSyncEventsRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() == static_cast<int>(ErrorCode::Ok)) {
        const QJsonObject data = response.value("data").toObject();
        emit eventsSynced(
            data.value("events").toArray(),
            data.value("lastSeq").toVariant().toLongLong(),
            data.value("hasMore").toBool());
    }
}

// ── QML 辅助方法 ─────────────────────────────────────────────────
QString NetworkManager::encryptPassword(const QString &password) const
{
    return EncryptionManager::encryptPassword(password);
}

QVariantList NetworkManager::toVariantList(const QJsonArray &array) const
{
    QVariantList result;
    for (const auto &val : array) {
        result.append(val.toVariant());
    }
    return result;
}
