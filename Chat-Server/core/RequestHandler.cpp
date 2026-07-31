#include "RequestHandler.h"
#include "database/DatabaseManager.h"
#include "EncryptionManager.h"
#include "LogSanitizer.h"
#include "SecureMemory.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QDateTime>
#include <QUuid>
#include <QMetaObject>

using namespace XYChat::Protocol;
using XYChat::Security::LogSanitizer;

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

// ── M3: 跨线程发送数据到客户端 ─────────────────────────────────────────────
void RequestHandler::sendRawData(const QByteArray &data)
{
    // 通过 Qt 队列连接跨线程调用
    QMetaObject::invokeMethod(this, [this, data]() {
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->write(data);
            m_socket->flush();
        }
    }, Qt::QueuedConnection);
}

// ── M5: 设置 TLS 配置 ───────────────────────────────────────────────────────
void RequestHandler::setSslConfiguration(const QSslConfiguration &config)
{
    m_sslConfig = config;
    m_tlsEnabled = true;
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

    m_socket = new QSslSocket();
    if (!m_socket->setSocketDescriptor(m_socketDescriptor)) {
        qDebug() << "[Handler] Failed to set socket descriptor";
        delete m_socket;
        m_socket = nullptr;
        emit finished();
        return;
    }

    // M5: 如果启用 TLS，启动服务端加密
    if (m_tlsEnabled) {
        m_socket->setSslConfiguration(m_sslConfig);
        connect(m_socket, &QSslSocket::encrypted, m_socket, [this]() {
            qDebug() << "[Handler] TLS handshake completed";
        });
        connect(m_socket, &QSslSocket::sslErrors, m_socket, [this](const QList<QSslError> &errors) {
            for (const auto &err : errors) {
                qWarning() << "[Handler] SSL error:" << err.errorString();
            }
            m_socket->disconnectFromHost();
        });
        m_socket->startServerEncryption();
    }

    m_idleTimer = new QTimer();
    m_idleTimer->setInterval(90000);
    m_idleTimer->setSingleShot(true);

    connect(m_socket, &QSslSocket::readyRead, m_socket, [this]() { onReadyRead(); });
    connect(m_socket, &QSslSocket::disconnected, m_socket, [this]() { quit(); });
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

    // M5: 重放保护检查（对所有业务请求）
    if (packet.messageType != MessageType::Ping && packet.messageType != MessageType::Pong) {
        if (!checkReplayProtection(json)) {
            sendResponse(packet.requestId, MessageType::Error, ErrorCode::InvalidRequest,
                         "Replay protection check failed");
            return;
        }
    }

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

    // M3 消息分发
    if (packet.messageType == MessageType::SearchUsersRequest || type == "search_users") {
        processSearchUsersRequest(packet, json);
        return;
    }
    if (packet.messageType == MessageType::AddContactRequest || type == "add_contact") {
        processAddContactRequest(packet, json);
        return;
    }
    if (packet.messageType == MessageType::GetContactsRequest || type == "get_contacts") {
        processGetContactsRequest(packet);
        return;
    }
    if (packet.messageType == MessageType::GetConversationsRequest || type == "get_conversations") {
        processGetConversationsRequest(packet);
        return;
    }
    if (packet.messageType == MessageType::SendMessageRequest || type == "send_message") {
        processSendMessageRequest(packet, json);
        return;
    }
    if (packet.messageType == MessageType::AckMessageRequest || type == "ack_message") {
        processAckMessageRequest(packet, json);
        return;
    }
    if (packet.messageType == MessageType::SyncMessagesRequest || type == "sync_messages") {
        processSyncMessagesRequest(packet, json);
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
             << "from" << LogSanitizer::maskIpAddress(ipAddr);

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
        qDebug() << "[Handler] Login failed for" << username << "(wrong password)";
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

// ── M3: 用户搜索 ───────────────────────────────────────────────────────────
void RequestHandler::processSearchUsersRequest(const Packet &packet, const QJsonObject &request)
{
    const QString query = request.value("query").toString().trimmed();
    if (query.isEmpty()) {
        sendResponse(packet.requestId, MessageType::SearchUsersResponse,
                     ErrorCode::InvalidRequest, "Query is required");
        return;
    }

    auto users = m_db->searchUsers(query);

    QJsonArray userArray;
    for (const auto &u : users) {
        if (u.id == m_authenticatedUserId) continue; // 排除自己
        QJsonObject obj;
        obj["userId"] = u.id;
        obj["username"] = u.username;
        userArray.append(obj);
    }

    QJsonObject data;
    data["users"] = userArray;
    sendResponse(packet.requestId, MessageType::SearchUsersResponse, ErrorCode::Ok,
                 "OK", data);
}

// ── M3: 添加联系人 ───────────────────────────────────────────────────────────
void RequestHandler::processAddContactRequest(const Packet &packet, const QJsonObject &request)
{
    const qint64 contactUserId = request.value("userId").toVariant().toLongLong();
    if (contactUserId <= 0 || contactUserId == m_authenticatedUserId) {
        sendResponse(packet.requestId, MessageType::AddContactResponse,
                     ErrorCode::CannotSendToSelf, "Invalid contact");
        return;
    }

    // 检查是否已是联系人
    auto contacts = m_db->getContacts(m_authenticatedUserId);
    for (const auto &c : contacts) {
        if (c.contactUserId == contactUserId) {
            sendResponse(packet.requestId, MessageType::AddContactResponse,
                         ErrorCode::ContactAlreadyExists, "Contact already exists");
            return;
        }
    }

    if (!m_db->addContact(m_authenticatedUserId, contactUserId)) {
        sendResponse(packet.requestId, MessageType::AddContactResponse,
                     ErrorCode::InternalError, "Failed to add contact");
        return;
    }

    QJsonObject data;
    data["contactUserId"] = contactUserId;
    sendResponse(packet.requestId, MessageType::AddContactResponse, ErrorCode::Ok,
                 "Contact added", data);
}

// ── M3: 获取联系人列表 ─────────────────────────────────────────────────────
void RequestHandler::processGetContactsRequest(const Packet &packet)
{
    auto contacts = m_db->getContacts(m_authenticatedUserId);

    QJsonArray contactArray;
    for (const auto &c : contacts) {
        QJsonObject obj;
        obj["userId"] = c.contactUserId;
        obj["username"] = c.contactUsername;
        obj["addedAt"] = c.createdAt;
        contactArray.append(obj);
    }

    QJsonObject data;
    data["contacts"] = contactArray;
    sendResponse(packet.requestId, MessageType::GetContactsResponse, ErrorCode::Ok,
                 "OK", data);
}

// ── M3: 获取会话列表 ─────────────────────────────────────────────────────
void RequestHandler::processGetConversationsRequest(const Packet &packet)
{
    auto conversations = m_db->getConversationsForUser(m_authenticatedUserId);

    QJsonArray convArray;
    for (const auto &c : conversations) {
        QJsonObject obj;
        obj["conversationId"] = c.id;
        obj["type"] = c.type;
        obj["peerUserId"] = c.peerUserId;
        obj["peerUsername"] = c.peerUsername;
        obj["lastMessage"] = c.lastMessage;
        obj["lastMessageId"] = c.lastMessageId;
        obj["lastMessageAt"] = c.lastMessageAt;
        obj["unreadCount"] = c.unreadCount;
        convArray.append(obj);
    }

    QJsonObject data;
    data["conversations"] = convArray;
    sendResponse(packet.requestId, MessageType::GetConversationsResponse, ErrorCode::Ok,
                 "OK", data);
}

// ── M3: 发送消息 ───────────────────────────────────────────────────────────
void RequestHandler::processSendMessageRequest(const Packet &packet, const QJsonObject &request)
{
    const qint64 targetUserId = request.value("toUserId").toVariant().toLongLong();
    const QString content = request.value("content").toString();
    const QString contentType = request.value("contentType").toString("text");

    if (targetUserId <= 0 || targetUserId == m_authenticatedUserId) {
        sendResponse(packet.requestId, MessageType::SendMessageResponse,
                     ErrorCode::CannotSendToSelf, "Invalid recipient");
        return;
    }
    if (content.isEmpty()) {
        sendResponse(packet.requestId, MessageType::SendMessageResponse,
                     ErrorCode::InvalidRequest, "Message content is empty");
        return;
    }

    // 获取或创建会话
    const qint64 convId = m_db->getOrCreatePrivateConversation(m_authenticatedUserId, targetUserId);
    if (convId < 0) {
        sendResponse(packet.requestId, MessageType::SendMessageResponse,
                     ErrorCode::InternalError, "Failed to create conversation");
        return;
    }

    // 存储消息
    const qint64 msgId = m_db->sendMessage(convId, m_authenticatedUserId, content, contentType);
    if (msgId < 0) {
        sendResponse(packet.requestId, MessageType::SendMessageResponse,
                     ErrorCode::InternalError, "Failed to send message");
        return;
    }

    // 构造消息通知包，用于转发给在线接收方
    QJsonObject notifyJson;
    notifyJson["messageId"] = msgId;
    notifyJson["conversationId"] = convId;
    notifyJson["senderId"] = m_authenticatedUserId;
    notifyJson["content"] = content;
    notifyJson["contentType"] = contentType;
    notifyJson["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    Packet notifyPacket;
    notifyPacket.messageType = MessageType::NewMessageNotification;
    notifyPacket.requestId = 0;
    notifyPacket.payload = QJsonDocument(notifyJson).toJson(QJsonDocument::Compact);

    // 通过信号通知 Server 转发
    emit messageForUser(targetUserId, PacketCodec::encode(notifyPacket));

    // 返回发送确认给发送方
    QJsonObject data;
    data["messageId"] = msgId;
    data["conversationId"] = convId;
    data["status"] = "sent";
    sendResponse(packet.requestId, MessageType::SendMessageResponse, ErrorCode::Ok,
                 "Message sent", data);
}

// ── M3: 确认消息 ───────────────────────────────────────────────────────────
void RequestHandler::processAckMessageRequest(const Packet &packet, const QJsonObject &request)
{
    const qint64 messageId = request.value("messageId").toVariant().toLongLong();
    const QString status = request.value("status").toString("delivered");

    if (messageId <= 0) {
        sendResponse(packet.requestId, MessageType::AckMessageResponse,
                     ErrorCode::InvalidRequest, "Invalid messageId");
        return;
    }

    auto msgOpt = m_db->getMessage(messageId);
    if (!msgOpt.has_value()) {
        sendResponse(packet.requestId, MessageType::AckMessageResponse,
                     ErrorCode::MessageNotFound, "Message not found");
        return;
    }

    m_db->updateMessageStatus(messageId, status);

    // 如果是已读状态，更新整个会话的已读进度
    if (status == "read") {
        m_db->updateMessagesReadStatus(msgOpt->conversationId, m_authenticatedUserId);
    }

    QJsonObject data;
    data["messageId"] = messageId;
    data["status"] = status;
    sendResponse(packet.requestId, MessageType::AckMessageResponse, ErrorCode::Ok,
                 "OK", data);
}

// ── M3: 同步消息 ───────────────────────────────────────────────────────────
void RequestHandler::processSyncMessagesRequest(const Packet &packet, const QJsonObject &request)
{
    const qint64 conversationId = request.value("conversationId").toVariant().toLongLong();
    const qint64 afterId = request.value("afterId").toVariant().toLongLong();
    const int limit = request.value("limit").toInt(100);

    if (conversationId <= 0) {
        sendResponse(packet.requestId, MessageType::SyncMessagesResponse,
                     ErrorCode::InvalidRequest, "Invalid conversationId");
        return;
    }

    // 验证用户是该会话的成员
    auto convOpt = m_db->getConversation(conversationId);
    if (!convOpt.has_value()) {
        sendResponse(packet.requestId, MessageType::SyncMessagesResponse,
                     ErrorCode::ConversationNotFound, "Conversation not found");
        return;
    }

    auto messages = m_db->syncMessages(conversationId, afterId, limit);

    QJsonArray msgArray;
    for (const auto &m : messages) {
        QJsonObject obj;
        obj["messageId"] = m.id;
        obj["conversationId"] = m.conversationId;
        obj["senderId"] = m.senderId;
        obj["senderUsername"] = m.senderUsername;
        obj["content"] = m.content;
        obj["contentType"] = m.contentType;
        obj["status"] = m.status;
        obj["createdAt"] = m.createdAt;
        msgArray.append(obj);
    }

    // 自动标记为已读
    m_db->updateMessagesReadStatus(conversationId, m_authenticatedUserId);

    QJsonObject data;
    data["conversationId"] = conversationId;
    data["messages"] = msgArray;
    data["hasMore"] = (messages.size() >= limit);
    sendResponse(packet.requestId, MessageType::SyncMessagesResponse, ErrorCode::Ok,
                 "OK", data);
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

// ── M5: 重放保护 ─────────────────────────────────────────────────────────────
bool RequestHandler::checkReplayProtection(const QJsonObject &request)
{
    // 检查时间戳
    const qint64 timestamp = request.value("timestamp").toVariant().toLongLong();
    if (timestamp > 0) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 diff = qAbs(now - timestamp);
        if (diff > ReplayTimestampToleranceSecs) {
            qDebug() << "[Handler] Replay protection: timestamp too old/new, diff=" << diff << "s";
            return false;
        }
    }

    // 检查 nonce
    const QString nonce = request.value("nonce").toString();
    if (!nonce.isEmpty()) {
        if (m_seenNonces.contains(nonce)) {
            qDebug() << "[Handler] Replay protection: duplicate nonce detected";
            return false;
        }
        m_seenNonces.insert(nonce);

        // 限制 nonce 缓存大小，避免内存无限增长
        if (m_seenNonces.size() > 10000) {
            m_seenNonces.clear();
        }
    }

    return true;
}
