#include "RequestHandler.h"
#include "database/DatabaseManager.h"
#include "EncryptionManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QUuid>

using namespace XYChat::Protocol;

// ── 构造 / 析构 ──────────────────────────────────────────────────────────────
RequestHandler::RequestHandler(qintptr socketDescriptor, QObject *parent)
    : QThread(parent)
    , m_socketDescriptor(socketDescriptor)
{
}

RequestHandler::~RequestHandler()
{
    delete m_db;
    m_db = nullptr;
}

// ── 线程入口 ─────────────────────────────────────────────────────────────────
void RequestHandler::run()
{
    // 每个线程创建独立数据库连接
    const QString connName = QString("handler_%1").arg(reinterpret_cast<quintptr>(this));
    m_db = new DatabaseManager(connName);
    if (!m_db->initialize()) {
        qCritical() << "[Handler] DB init failed for" << connName;
        emit finished();
        return;
    }

    m_socket = new QTcpSocket();
    if (!m_socket->setSocketDescriptor(m_socketDescriptor)) {
        qDebug() << "[Handler] Failed to set socket descriptor";
        delete m_socket;
        m_socket = nullptr;
        emit finished();
        return;
    }

    m_idleTimer = new QTimer();
    m_idleTimer->setInterval(90000);
    m_idleTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::readyRead, m_socket, [this]() { onReadyRead(); });
    connect(m_socket, &QTcpSocket::disconnected, m_socket, [this]() { quit(); });
    connect(m_idleTimer, &QTimer::timeout, m_idleTimer, [this]() { onIdleTimeout(); });
    m_idleTimer->start();

    exec();

    // 连接断开时，清理 session
    if (m_currentSessionId > 0) {
        emit userLoggedOut(m_authenticatedUserId, m_currentSessionId);
    }

    delete m_idleTimer;
    m_idleTimer = nullptr;
    delete m_socket;
    m_socket = nullptr;
}

// ── 数据接收 ─────────────────────────────────────────────────────────────────
void RequestHandler::onReadyRead()
{
    m_idleTimer->start();
    m_codec.appendData(m_socket->readAll());

    while (true) {
        Packet packet;
        QString errorMessage;
        const PacketCodec::DecodeStatus status = m_codec.nextPacket(packet, &errorMessage);
        if (status == PacketCodec::DecodeStatus::NeedMoreData) {
            return;
        }
        if (status == PacketCodec::DecodeStatus::InvalidData) {
            sendResponse(0, MessageType::Error, ErrorCode::InvalidRequest, errorMessage);
            m_socket->disconnectFromHost();
            return;
        }

        processPacket(packet);
    }
}

void RequestHandler::onIdleTimeout()
{
    sendResponse(0, MessageType::Error, ErrorCode::Timeout, "Idle timeout");
    m_socket->disconnectFromHost();
}

// ── 包分发 ───────────────────────────────────────────────────────────────────
void RequestHandler::processPacket(const Packet &packet)
{
    // Ping/Pong 不需要认证
    if (packet.messageType == MessageType::Ping) {
        Packet pong;
        pong.messageType = MessageType::Pong;
        pong.requestId = packet.requestId;
        sendPacket(pong);
        return;
    }

    // 解析 JSON payload
    QJsonParseError parseError;
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(packet.payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        sendResponse(packet.requestId, MessageType::Error, ErrorCode::InvalidRequest,
                     "Invalid JSON payload");
        return;
    }
    const QJsonObject json = jsonDoc.object();
    const QString type = json.value("type").toString();

    // 不需要认证的请求
    if (packet.messageType == MessageType::RegisterRequest || type == "register") {
        processRegisterRequest(packet, json);
        return;
    }
    if (packet.messageType == MessageType::LoginRequest || type == "login") {
        processLoginRequest(packet, json);
        return;
    }

    // 需要认证的请求：先验证 session token
    if (!validateSession()) {
        sendResponse(packet.requestId, MessageType::Error, ErrorCode::SessionInvalid,
                     "Authentication required");
        return;
    }

    // 更新 session 活跃时间
    m_db->updateSessionLastActive(m_currentSessionId);

    if (packet.messageType == MessageType::LogoutRequest || type == "logout") {
        processLogoutRequest(packet);
        return;
    }
    if (packet.messageType == MessageType::TokenRenewRequest || type == "token_renew") {
        processTokenRenewRequest(packet);
        return;
    }
    if (packet.messageType == MessageType::ForceLogoutRequest || type == "force_logout") {
        processForceLogoutRequest(packet, json);
        return;
    }

    sendResponse(packet.requestId, MessageType::Error, ErrorCode::InvalidRequest,
                 QString("Unknown request type: %1").arg(type));
}

// ── 登录 ─────────────────────────────────────────────────────────────────────
void RequestHandler::processLoginRequest(const Packet &packet, const QJsonObject &request)
{
    const QString username = request.value("username").toString().trimmed();
    const QString clientPassword = request.value("password").toString();
    const QString deviceId = request.value("deviceId").toString();
    const QString clientVersion = request.value("clientVersion").toString();
    const QString platform = request.value("platform").toString();
    const QString ipAddr = m_socket->peerAddress().toString();

    qDebug() << "[Handler] Login request" << packet.requestId << username
             << "from" << ipAddr;

    if (username.isEmpty() || clientPassword.isEmpty()) {
        sendResponse(packet.requestId, MessageType::LoginResponse, ErrorCode::InvalidRequest,
                     "Username and password are required");
        return;
    }

    // 查找用户
    auto userOpt = m_db->getUserByUsername(username);
    if (!userOpt.has_value()) {
        m_db->recordLoginAttempt(0, ipAddr, false, "User not found");
        sendResponse(packet.requestId, MessageType::LoginResponse,
                     ErrorCode::AuthenticationFailed,
                     "Invalid username or password");
        return;
    }

    const UserInfo &user = userOpt.value();

    // 限流检查
    if (checkRateLimit(ipAddr, user.id)) {
        sendResponse(packet.requestId, MessageType::LoginResponse,
                     ErrorCode::LoginRateLimited,
                     "Too many failed login attempts. Please try again later.");
        return;
    }

    // 验证 PBKDF2 密码
    if (!EncryptionManager::verifyPassword(clientPassword, user.passwordHash)) {
        m_db->recordLoginAttempt(user.id, ipAddr, false, "Wrong password");
        sendResponse(packet.requestId, MessageType::LoginResponse,
                     ErrorCode::AuthenticationFailed,
                     "Invalid username or password");
        qDebug() << "[Handler] Login failed for" << username << "wrong password";
        return;
    }

    // 生成 session token
    const QString token = EncryptionManager::generateToken();
    const QString tokenHash = EncryptionManager::hashToken(token);

    // 注册设备
    m_db->registerDevice(user.id, deviceId, QString("Device-%1").arg(deviceId.left(8)), platform);

    // 创建 session
    const qint64 sessionId = m_db->createSession(user.id, deviceId, tokenHash, ipAddr);
    if (sessionId < 0) {
        sendResponse(packet.requestId, MessageType::LoginResponse,
                     ErrorCode::InternalError, "Failed to create session");
        return;
    }

    // 记录成功审计
    m_db->recordLoginAttempt(user.id, ipAddr, true);

    // 更新认证状态
    m_authenticatedUserId = user.id;
    m_currentSessionId = sessionId;
    m_currentDeviceId = deviceId;

    // 返回响应
    QJsonObject data;
    data["username"] = user.username;
    data["userId"] = user.id;
    data["token"] = token;
    data["expiresAt"] = QDateTime::currentDateTimeUtc()
                                            .addSecs(86400 * 7)
                                            .toString(Qt::ISODate);

    emit userLoggedIn(user.id, sessionId, deviceId);

    sendResponse(packet.requestId, MessageType::LoginResponse, ErrorCode::Ok,
                 "OK", data);
    qDebug() << "[Handler] Login successful for" << username;
}

// ── 注册 ─────────────────────────────────────────────────────────────────────
void RequestHandler::processRegisterRequest(const Packet &packet, const QJsonObject &request)
{
    const QString username = request.value("username").toString().trimmed();
    const QString password = request.value("password").toString();
    const QString email = request.value("email").toString().trimmed();
    const QString phone = request.value("phone").toString().trimmed();
    const QString ipAddr = m_socket->peerAddress().toString();

    qDebug() << "[Handler] Register request" << packet.requestId << username;

    // 输入校验
    if (username.isEmpty() || password.isEmpty()) {
        sendResponse(packet.requestId, MessageType::RegisterResponse,
                     ErrorCode::InvalidRequest,
                     "Username and password are required");
        return;
    }
    if (username.size() < 3 || username.size() > 32) {
        sendResponse(packet.requestId, MessageType::RegisterResponse,
                     ErrorCode::InvalidRequest,
                     "Username must be 3-32 characters");
        return;
    }
    if (password.size() < 6) {
        sendResponse(packet.requestId, MessageType::RegisterResponse,
                     ErrorCode::InvalidRequest,
                     "Password must be at least 6 characters");
        return;
    }

    // 检查用户名是否已存在
    if (m_db->userExists(username)) {
        sendResponse(packet.requestId, MessageType::RegisterResponse,
                     ErrorCode::AccountAlreadyExists,
                     "Username already exists");
        return;
    }

    // PBKDF2 哈希密码
    const QString passwordHash = EncryptionManager::hashPasswordWithSalt(password);

    // 创建用户
    const qint64 userId = m_db->registerUser(username, email, phone, passwordHash);
    if (userId < 0) {
        sendResponse(packet.requestId, MessageType::RegisterResponse,
                     ErrorCode::InternalError, "Failed to create account");
        return;
    }

    QJsonObject data;
    data["userId"] = userId;
    data["username"] = username;

    sendResponse(packet.requestId, MessageType::RegisterResponse, ErrorCode::Ok,
                 "Account created successfully", data);
    qDebug() << "[Handler] Registered user" << username << "id=" << userId;
}

// ── 登出 ─────────────────────────────────────────────────────────────────────
void RequestHandler::processLogoutRequest(const Packet &packet)
{
    const qint64 sessionId = m_currentSessionId;
    const qint64 userId = m_authenticatedUserId;

    m_db->deleteSession(sessionId);
    emit userLoggedOut(userId, sessionId);

    m_authenticatedUserId = 0;
    m_currentSessionId = 0;

    QJsonObject data;
    sendResponse(packet.requestId, MessageType::LogoutResponse, ErrorCode::Ok,
                 "Logged out", data);
    qDebug() << "[Handler] User" << userId << "logged out";
}

// ── Token 续期 ───────────────────────────────────────────────────────────────
void RequestHandler::processTokenRenewRequest(const Packet &packet)
{
    const qint64 oldSessionId = m_currentSessionId;
    const qint64 userId = m_authenticatedUserId;

    // 生成新 token
    const QString newToken = EncryptionManager::generateToken();
    const QString newTokenHash = EncryptionManager::hashToken(newToken);

    // 删除旧 session，创建新 session
    m_db->deleteSession(oldSessionId);

    const QString ipAddr = m_socket->peerAddress().toString();
    const qint64 newSessionId = m_db->createSession(userId, m_currentDeviceId, newTokenHash, ipAddr);
    if (newSessionId < 0) {
        sendResponse(packet.requestId, MessageType::TokenRenewResponse,
                     ErrorCode::InternalError, "Failed to renew token");
        return;
    }

    m_currentSessionId = newSessionId;

    QJsonObject data;
    data["token"] = newToken;
    data["expiresAt"] = QDateTime::currentDateTimeUtc()
                                            .addSecs(86400 * 7)
                                            .toString(Qt::ISODate);

    sendResponse(packet.requestId, MessageType::TokenRenewResponse, ErrorCode::Ok,
                 "Token renewed", data);
    qDebug() << "[Handler] Token renewed for user" << userId;
}

// ── 强制下线 ─────────────────────────────────────────────────────────────────
void RequestHandler::processForceLogoutRequest(const Packet &packet, const QJsonObject &request)
{
    const qint64 targetUserId = request.value("userId").toVariant().toLongLong();

    if (targetUserId <= 0) {
        sendResponse(packet.requestId, MessageType::ForceLogoutResponse,
                     ErrorCode::InvalidRequest, "Invalid userId");
        return;
    }

    // 删除目标用户所有 session
    m_db->deleteSessionsByUserId(targetUserId);
    emit userLoggedOut(targetUserId, 0); // sessionId=0 表示全部清除

    QJsonObject data;
    data["affectedUserId"] = targetUserId;

    sendResponse(packet.requestId, MessageType::ForceLogoutResponse, ErrorCode::Ok,
                 "User forced logout", data);
    qDebug() << "[Handler] Force logout user" << targetUserId;
}

// ── Session 验证 ─────────────────────────────────────────────────────────────
bool RequestHandler::validateSession()
{
    if (m_currentSessionId > 0 && m_authenticatedUserId > 0) {
        return true;
    }
    return false;
}

// ── 限流检查 ─────────────────────────────────────────────────────────────────
bool RequestHandler::checkRateLimit(const QString &ipAddress, qint64 userId)
{
    const int ipFails = m_db->recentFailedLoginCount(ipAddress, RateLimitWindowSeconds);
    if (ipFails >= MaxFailedLoginsPerIP) {
        qDebug() << "[Handler] Rate limit: IP" << ipAddress << "has" << ipFails << "failures";
        return true;
    }
    if (userId > 0) {
        const int userFails = m_db->recentFailedLoginCountForUser(userId, RateLimitWindowSeconds);
        if (userFails >= MaxFailedLoginsPerUser) {
            qDebug() << "[Handler] Rate limit: user" << userId << "has" << userFails << "failures";
            return true;
        }
    }
    return false;
}

// ── 响应工具 ─────────────────────────────────────────────────────────────────
void RequestHandler::sendResponse(quint64 requestId,
                                  MessageType messageType,
                                  ErrorCode code,
                                  const QString &message,
                                  const QJsonObject &data)
{
    QJsonObject response;
    response["code"] = static_cast<int>(code);
    response["message"] = message;
    response["data"] = data;

    Packet packet;
    packet.messageType = messageType;
    packet.requestId = requestId;
    packet.payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    sendPacket(packet);
}

void RequestHandler::sendPacket(const Packet &packet)
{
    const QByteArray encoded = PacketCodec::encode(packet);
    if (encoded.isEmpty()) {
        qWarning() << "[Handler] Failed to encode packet" << packet.requestId;
        return;
    }

    m_socket->write(encoded);
    m_socket->flush();
}
