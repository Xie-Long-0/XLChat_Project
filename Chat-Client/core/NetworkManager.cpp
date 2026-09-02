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
#include "GroupE2eeCrypto.h"

using namespace XYChat::Protocol;
using namespace XYChat::Security;
using XYChat::Security::GroupE2eeCrypto;

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

// 登录
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

// 注册
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

// 登出
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

// Token 续期
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

// 连接回调
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
    // M6: 密钥交换在途状态随连接重置，重新登录后重新引导
    m_e2eeReady = false;
    m_e2eeBootstrapPending = false;
    m_pendingRegisterKeysRequestId = 0;
    m_pendingFetchKeysRequestId = 0;
    m_fetchKeysTargetUserId = 0;
    // M7b: 群 E2EE 引导状态与内存缓存重置（Sender Key 保留在 LocalStore）
    m_pendingFetchGroupKeysRequestId = 0;
    m_fetchGroupKeysTargetConvId = 0;
    m_pendingGroupDistributions.clear();
    m_groupSenderKeys.clear();
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

// M5: SSL 错误处理
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

    // M5.5: fail-closed：TLS 不可用时拒绝连接，
    // 除非显式设置环境变量 XYCHAT_ALLOW_PLAINTEXT=1（仅限开发）
    if (!m_tlsEnabled) {
        if (qEnvironmentVariable("XYCHAT_ALLOW_PLAINTEXT") == "1") {
            qWarning() << "[NetMgr] Connecting in PLAINTEXT development mode."
                       << "Do not use in production.";
        } else {
            const QString err = 
                "TLS unavailable: CA certificate not found. "
                "Refusing plaintext connection (set XYCHAT_ALLOW_PLAINTEXT=1 for development only).";
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

// M5: TLS 初始化
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

// 发送登录请求
void NetworkManager::sendLoginRequest()
{
    QJsonObject json;
    json["type"] = "login";
    json["username"] = m_pendingUsername;
    json["password"] = m_pendingPassword;
    json["clientVersion"] = "0.2.0";
    json["platform"] = QSysInfo::productType();
    m_localDeviceId = QString::fromLatin1(QSysInfo::machineUniqueId().toHex());
    json["deviceId"] = m_localDeviceId;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::LoginRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);

    m_pendingLoginRequestId = packet.requestId;
    setState(ConnectionState::LoggingIn);
    sendPacket(packet);
}

// 发送注册请求
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

// 包分发
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
    // M6
    case MessageType::RegisterKeysResponse:
        handleRegisterKeysResponse(packet);
        break;
    case MessageType::FetchKeysResponse:
        handleFetchKeysResponse(packet);
        break;
    // M7b: 群 E2EE 密钥包拉取
    case MessageType::FetchGroupKeysResponse:
        handleFetchGroupKeysResponse(packet);
        break;
    // M7a: 群组响应与推送
    case MessageType::CreateGroupResponse:
        handleCreateGroupResponse(packet);
        break;
    case MessageType::InviteGroupMembersResponse:
        handleInviteGroupMembersResponse(packet);
        break;
    case MessageType::LeaveGroupResponse:
        handleLeaveGroupResponse(packet);
        break;
    case MessageType::KickGroupMemberResponse:
        handleKickGroupMemberResponse(packet);
        break;
    case MessageType::GetGroupInfoResponse:
        handleGetGroupInfoResponse(packet);
        break;
    case MessageType::GroupChangedNotification:
        handleGroupChangedNotification(packet);
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

// 登录响应
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
        // M6.5: 打开本地加密缓存（加载持久化 outbox、立即展示缓存会话、游标增量同步）
        openLocalStore();
        // M5.5: 登录成功后重发 outbox 中未确认的消息（幂等键保证不重复）
        flushOutbox();
        return;
    }

    setState(ConnectionState::Connected);
    const QString message = response.value("message").toString("Unknown error");
    emit loginFailed(message);
}

// 注册响应
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

// 登出响应
void NetworkManager::handleLogoutResponse(const Packet &packet)
{
    Q_UNUSED(packet);
    resetAuthState();
    m_reconnectEnabled = false;
    setState(ConnectionState::Connected);
    emit logoutFinished();
}

// Token 续期响应
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

// 工具
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
    m_e2eeReady = false;
    m_e2eeBootstrapPending = false;
    m_pendingRegisterKeysRequestId = 0;
    m_pendingFetchKeysRequestId = 0;
    m_fetchKeysTargetUserId = 0;
    m_fetchBackoffUntil.clear();
    m_serverPrekeyRemaining = -1;
    m_decryptCache.clear();
    m_decryptCacheLoaded = false;
    m_pendingFetchGroupKeysRequestId = 0;
    m_fetchGroupKeysTargetConvId = 0;
    m_pendingGroupDistributions.clear();
    m_groupSenderKeys.clear();
    XYChat::Security::SecureMemory::wipe(m_identityKey.privateKey);
    m_identityKey = {};
    for (auto &pk : m_localPrekeys) {
        XYChat::Security::SecureMemory::wipe(pk.privateKey);
    }
    m_localPrekeys.clear();
    // M6.5: 登出清除本地用户数据（消息/会话/outbox/同步游标）；
    // 解密缓存与存储密钥属 E2EE 密钥材料，必须保留：登出重登时一次性
    // 预密钥已消费不可恢复，对方消息只能靠解密缓存兜底（M6 产品承诺）；
    // E2EE 身份密钥同样不在此列，仍由 KeyStorage 保留供下次登录复用
    if (m_localStore.isOpen()) {
        m_localStore.clearUserData();
        m_localStore.close();
    }
    emit sessionChanged();
}

// M5: 重放保护
void NetworkManager::addReplayProtection(QJsonObject &json)
{
    json["timestamp"] = QDateTime::currentSecsSinceEpoch();
    json["nonce"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// M3: 用户搜索
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

// M3: 添加联系人
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

// M3: 获取联系人列表
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

// M3: 获取会话列表
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

// M3: 发送消息
QString NetworkManager::sendMessage(qint64 toUserId, const QString &content)
{
    // M5.5: 客户端生成幂等键，重试/重连重发不会产生重复消息
    // M4.5: 返回幂等键供 QML 跟踪乐观消息气泡状态
    const QString clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // M6.5: outbox 加密落库，重启后不丢未发送消息
    if (!m_localStore.isOpen()) {
        // 登录前排队的场景：尝试以待登录账号打开本地库
        const QString user = m_state == ConnectionState::Authenticated
            ? m_username : m_pendingUsername;
        const QString deviceId = m_localDeviceId.isEmpty()
            ? QString::fromLatin1(QSysInfo::machineUniqueId().toHex())
            : m_localDeviceId;
        if (!user.isEmpty() && !deviceId.isEmpty()) {
            m_localStore.open(user, deviceId);
        }
    }
    m_localStore.addOutboxItem(clientMessageId, toUserId, content);

    if (m_state != ConnectionState::Authenticated) {
        // M5.5: 未认证时进入 outbox，登录成功后自动重发
        m_outbox.append({clientMessageId, toUserId, 0, content});
        return clientMessageId;
    }

    m_outbox.append({clientMessageId, toUserId, 0, content});
    flushOutbox();
    return clientMessageId;
}

// M7a: 发送群消息（明文；返回幂等键供 QML 乐观消息跟踪）
QString NetworkManager::sendGroupMessage(qint64 conversationId, const QString &content)
{
    if (conversationId <= 0 || content.trimmed().isEmpty()) {
        emit messageSendFailed("Invalid group message");
        return {};
    }

    const QString clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 与私聊一致：outbox 加密落库，重启后不丢未发送消息
    if (!m_localStore.isOpen()) {
        const QString user = m_state == ConnectionState::Authenticated
            ? m_username : m_pendingUsername;
        const QString deviceId = m_localDeviceId.isEmpty()
            ? QString::fromLatin1(QSysInfo::machineUniqueId().toHex())
            : m_localDeviceId;
        if (!user.isEmpty() && !deviceId.isEmpty()) {
            m_localStore.open(user, deviceId);
        }
    }
    m_localStore.addOutboxItem(clientMessageId, 0, content, conversationId);

    m_outbox.append({clientMessageId, 0, conversationId, content});
    if (m_state == ConnectionState::Authenticated) {
        flushOutbox();
    }
    return clientMessageId;
}

// M5.5: 将 outbox 中未确认的消息逐条发送（同一 clientMessageId 只保留一份）
// M6: 发送前先拉取接收方密钥包，正文加密为 envelope 后再提交
void NetworkManager::flushOutbox()
{
    if (m_state != ConnectionState::Authenticated) {
        return;
    }

    // M6/M7b: 身份密钥尚未注册时先引导，完成后会再次 flush
    if (!m_e2eeReady) {
        bootstrapE2ee();
        return;
    }

    // 已在途的 clientMessageId 不重复发
    QSet<QString> inFlight;
    for (auto it = m_pendingSendByRequestId.constBegin();
         it != m_pendingSendByRequestId.constEnd(); ++it) {
        inFlight.insert(it.value());
    }

    // M7b: 群消息使用 Sender Key E2EE
    for (const OutboxItem &item : std::as_const(m_outbox)) {
        if (item.conversationId <= 0 || inFlight.contains(item.clientMessageId)) {
            continue;
        }
        GroupE2eeCrypto::SenderKey key;
        if (ensureGroupSenderKey(item.conversationId, key)) {
            const QString envelope = encryptGroupMessage(item.conversationId, item.content);
            if (envelope.isEmpty()) {
                qWarning() << "[NetMgr] Failed to encrypt group message"
                           << item.clientMessageId;
                continue;
            }
            QJsonObject json;
            json["type"] = "send_message";
            json["conversationId"] = item.conversationId;
            json["content"] = envelope;
            json["contentType"] = "e2ee_group";
            json["clientMessageId"] = item.clientMessageId;
            addReplayProtection(json);

            Packet groupPacket;
            groupPacket.messageType = MessageType::SendMessageRequest;
            groupPacket.requestId = nextRequestId();
            groupPacket.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
            m_pendingSendByRequestId.insert(groupPacket.requestId, item.clientMessageId);
            sendPacket(groupPacket);
        } else if (m_pendingFetchGroupKeysRequestId == 0) {
            // 本群尚无 Sender Key：先拉取成员密钥包，响应后继续 flush
            sendFetchGroupKeysRequest(item.conversationId);
            return;
        }
    }

    // 按目标用户汇总待发消息，逐用户拉取密钥包（每次 FetchKeys 的预密钥
    // 仅供一条消息使用，后续消息在响应回调中继续触发 flush）；
    // 处于退避期的目标（对方尚未注册密钥等）暂不拉取
    const qint64 nowSecs = QDateTime::currentSecsSinceEpoch();
    QSet<qint64> targets;
    for (const OutboxItem &item : std::as_const(m_outbox)) {
        if (item.conversationId > 0) {
            continue; // M7a: 群消息已在上方直发
        }
        if (!inFlight.contains(item.clientMessageId)) {
            targets.insert(item.toUserId);
        }
    }
    for (qint64 target : targets) {
        if (m_pendingFetchKeysRequestId != 0) {
            break; // 同一时刻只保持一个在途 FetchKeys，响应后继续
        }
        if (m_fetchBackoffUntil.value(target, 0) > nowSecs) {
            continue; // 等待对方注册密钥，稍后由定时器重试
        }
        sendFetchKeysRequest(target);
    }
}

// M6: E2EE 引导（加载/生成身份密钥，补齐预密钥，注册到服务端）
void NetworkManager::bootstrapE2ee()
{
    using namespace XYChat::Security;

    if (m_e2eeBootstrapPending || m_localDeviceId.isEmpty() || m_username.isEmpty()) {
        return;
    }

    // 加载或生成本机身份密钥对
    if (!m_identityKey.valid) {
        QByteArray priv = KeyStorage::loadIdentityPrivateKey(m_username, m_localDeviceId);
        if (priv.isEmpty()) {
            m_identityKey = E2eeCrypto::generateX25519KeyPair();
            if (!m_identityKey.valid) {
                qCritical() << "[NetMgr] Failed to generate identity keypair";
                return;
            }
            if (!KeyStorage::saveIdentityPrivateKey(m_username, m_localDeviceId,
                                                    m_identityKey.privateKey)) {
                // 持久化失败时密钥仅存在于内存，重启后重新生成（服务端会
                // 因身份变更废弃旧预密钥，语义上仍然安全）
                qWarning() << "[NetMgr] Failed to persist identity key,"
                              "it will be regenerated on next launch";
            }
            // 新身份世代：旧预密钥与新身份不匹配，全部丢弃
            for (auto &pk : m_localPrekeys) {
                SecureMemory::wipe(pk.privateKey);
            }
            m_localPrekeys.clear();
            qInfo() << "[NetMgr] Generated new E2EE identity key";
        } else {
            m_identityKey = E2eeCrypto::keyPairFromPrivateKey(priv);
            SecureMemory::wipe(priv);
            if (!m_identityKey.valid) {
                // 审查修复：存储的身份密钥损坏时重新生成，避免发送功能永久阻塞
                qWarning() << "[NetMgr] Stored identity key invalid, regenerating";
                m_identityKey = E2eeCrypto::generateX25519KeyPair();
                if (!m_identityKey.valid) {
                    qCritical() << "[NetMgr] Failed to regenerate identity keypair";
                    return;
                }
                KeyStorage::saveIdentityPrivateKey(m_username, m_localDeviceId,
                                                   m_identityKey.privateKey);
                for (auto &pk : m_localPrekeys) {
                    SecureMemory::wipe(pk.privateKey);
                }
                m_localPrekeys.clear();
            }
        }
    }

    // 加载本地预密钥，低于阈值时补齐并随注册一并上传公钥
    if (m_localPrekeys.isEmpty()) {
        m_localPrekeys = KeyStorage::loadPrekeys(m_username, m_localDeviceId);
    }

    // 加载持久化解密缓存：一次性预密钥解密后即删除，重新登录后
    // 历史消息依靠此缓存恢复明文（修复：登出重登后无法解密旧消息）
    // M6.5: 缓存已归口 LocalStore，仅本地库不可用时回退 KeyStorage 文件
    if (!m_decryptCacheLoaded) {
        if (!m_localStore.isOpen()) {
            m_decryptCache = KeyStorage::loadDecryptCache(m_username, m_localDeviceId);
        }
        m_decryptCacheLoaded = true;
    }

    m_e2eeBootstrapPending = true;
    sendRegisterKeysRequest();
}

void NetworkManager::sendRegisterKeysRequest()
{
    using namespace XYChat::Security;

    QJsonObject json;
    json["type"] = "register_keys";
    json["identityPub"] = QString::fromLatin1(m_identityKey.publicKey.toBase64());

    // 预密钥补齐：同时参考本地存量与服务端报告余量（服务端消费/废弃对
    // 本地不可见，仅看本地会导致服务端枯竭后永不补齐）
    constexpr int PrekeyTarget = 20;
    constexpr int PrekeyLowWatermark = 5;
    const bool localLow = m_localPrekeys.size() < PrekeyLowWatermark;
    const bool serverLow = m_serverPrekeyRemaining >= 0
        && m_serverPrekeyRemaining < PrekeyLowWatermark;
    if (localLow || serverLow) {
        QJsonArray pubs;
        const int uploadCount = PrekeyTarget - (serverLow ? m_serverPrekeyRemaining
                                                          : m_localPrekeys.size());
        for (int i = 0; i < uploadCount; ++i) {
            const auto kp = E2eeCrypto::generateX25519KeyPair();
            if (!kp.valid) {
                break;
            }
            KeyStorage::PrekeyEntry entry;
            entry.publicKey = kp.publicKey;
            entry.privateKey = kp.privateKey;
            m_localPrekeys.append(entry);
            pubs.append(QString::fromLatin1(kp.publicKey.toBase64()));
        }
        KeyStorage::savePrekeys(m_username, m_localDeviceId, m_localPrekeys);
        json["prekeys"] = pubs;
    }

    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::RegisterKeysRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingRegisterKeysRequestId = packet.requestId;
    sendPacket(packet);
}

void NetworkManager::handleRegisterKeysResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingRegisterKeysRequestId) {
        return;
    }
    m_pendingRegisterKeysRequestId = 0;
    m_e2eeBootstrapPending = false;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() == static_cast<int>(ErrorCode::Ok)) {
        m_e2eeReady = true;
        const QJsonObject data = response.value("data").toObject();
        m_serverPrekeyRemaining = data.value("remainingPrekeys").toInt();
        qInfo() << "[NetMgr] E2EE keys registered, remaining prekeys:"
                << m_serverPrekeyRemaining;
        // 引导完成后继续发送 outbox
        flushOutbox();
    } else {
        qWarning() << "[NetMgr] RegisterKeys failed:"
                   << response.value("message").toString() << ", retry in 3s";
        QTimer::singleShot(3000, this, [this]() {
            if (m_state == ConnectionState::Authenticated && !m_e2eeReady) {
                bootstrapE2ee();
            }
        });
    }
}

void NetworkManager::sendFetchKeysRequest(qint64 toUserId)
{
    QJsonObject json;
    json["type"] = "fetch_keys";
    json["userId"] = toUserId;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::FetchKeysRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingFetchKeysRequestId = packet.requestId;
    m_fetchKeysTargetUserId = toUserId;
    sendPacket(packet);
}

void NetworkManager::handleFetchKeysResponse(const Packet &packet)
{
    using namespace XYChat::Security;

    if (packet.requestId != m_pendingFetchKeysRequestId) {
        return;
    }
    const qint64 target = m_fetchKeysTargetUserId;
    m_pendingFetchKeysRequestId = 0;
    m_fetchKeysTargetUserId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    const int code = response.value("code").toInt();
    if (code != static_cast<int>(ErrorCode::Ok)) {
        const QString message = response.value("message").toString("Key bundle unavailable");
        if (code == static_cast<int>(ErrorCode::CannotSendToSelf)) {
            // 确定性失败：移除该用户的待发项（含持久化 outbox）并上报
            for (int i = m_outbox.size() - 1; i >= 0; --i) {
                if (m_outbox.at(i).toUserId == target) {
                    m_localStore.removeOutboxItem(m_outbox.at(i).clientMessageId);
                    m_outbox.removeAt(i);
                }
            }
            emit messageSendFailed(message);
        } else if (code == static_cast<int>(ErrorCode::KeyBundleUnavailable)
                   || code == static_cast<int>(ErrorCode::AccountNotFound)) {
            // 修复：对方尚未注册 E2EE 密钥（从未登录/未上线）是产品上的
            // 可恢复状态，保留 outbox 并定期重试，对方首次登录注册密钥后送达；
            // 不能直接丢弃消息（此前行为导致离线发送永久丢失）
            qInfo() << "[NetMgr] Target" << target << "has no keys yet,"
                    << "keeping outbox and retrying in 30s:" << message;
            m_fetchBackoffUntil.insert(target, QDateTime::currentSecsSinceEpoch() + 30);
            QTimer::singleShot(30000, this, [this]() { flushOutbox(); });
        } else {
            // 瞬时失败（限流/内部错误等）：保留 outbox，短退避后重试
            qWarning() << "[NetMgr] FetchKeys transient failure:" << message
                       << ", retry in 3s";
            m_fetchBackoffUntil.insert(target, QDateTime::currentSecsSinceEpoch() + 3);
            QTimer::singleShot(3000, this, [this]() { flushOutbox(); });
        }
        flushOutbox();
        return;
    }

    m_fetchBackoffUntil.remove(target);

    const QJsonArray bundles = response.value("data").toObject().value("bundles").toArray();
    if (bundles.isEmpty()) {
        emit messageSendFailed("Empty key bundle");
        flushOutbox();
        return;
    }

    // TOFU：首次记录对方身份公钥指纹，变更时告警（不阻塞发送）
    {
        const QByteArray identityPub =
            QByteArray::fromBase64(bundles.first().toObject()
                                       .value("identityPub").toString().toLatin1());
        const QString fingerprint = E2eeCrypto::publicKeyFingerprint(identityPub);
        const QString stored = KeyStorage::loadPeerFingerprint(target);
        if (stored.isEmpty()) {
            KeyStorage::savePeerFingerprint(target, fingerprint);
        } else if (stored != fingerprint) {
            qWarning() << "[NetMgr] Peer identity key changed for user" << target;
            KeyStorage::savePeerFingerprint(target, fingerprint);
            emit peerIdentityChanged(target);
        }
    }

    // 已在途的 clientMessageId 不重复发
    QSet<QString> inFlight;
    for (auto it = m_pendingSendByRequestId.constBegin();
         it != m_pendingSendByRequestId.constEnd(); ++it) {
        inFlight.insert(it.value());
    }

    // 认领的预密钥仅供一条消息使用：本轮只处理该用户的第一条待发项，
    // 发送完成后 flushOutbox 会为下一条重新拉取密钥包
    for (int i = 0; i < m_outbox.size(); ++i) {
        const OutboxItem &item = m_outbox.at(i);
        if (item.toUserId != target || inFlight.contains(item.clientMessageId)) {
            continue;
        }

        const QString envelope = encryptForUser(target, bundles, item.content);
        if (envelope.isEmpty()) {
            qWarning() << "[NetMgr] E2EE encryption failed for message"
                       << item.clientMessageId;
            m_localStore.removeOutboxItem(item.clientMessageId);
            m_outbox.removeAt(i);
            emit messageSendFailed("End-to-end encryption failed");
            break;
        }

        QJsonObject json;
        json["type"] = "send_message";
        json["toUserId"] = item.toUserId;
        json["content"] = envelope;
        json["contentType"] = "text";
        json["clientMessageId"] = item.clientMessageId;
        addReplayProtection(json);

        Packet sendPacketMsg;
        sendPacketMsg.messageType = MessageType::SendMessageRequest;
        sendPacketMsg.requestId = nextRequestId();
        sendPacketMsg.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
        m_pendingSendByRequestId.insert(sendPacketMsg.requestId, item.clientMessageId);
        sendPacket(sendPacketMsg);
        break;
    }

    // 继续处理其他目标用户的待发消息
    flushOutbox();
}

// 逐设备加密：每条消息生成临时密钥对，shared = ECDH(eph, prekey) || ECDH(eph, identity)
QString NetworkManager::encryptForUser(qint64 toUserId, const QJsonArray &bundles,
                                       const QString &plaintext)
{
    Q_UNUSED(toUserId);
    using namespace XYChat::Security;

    const QByteArray plainBytes = plaintext.toUtf8();
    QList<E2eeCrypto::EnvelopeEntry> entries;

    for (const QJsonValue &value : bundles) {
        const QJsonObject bundle = value.toObject();
        const QString deviceId = bundle.value("deviceId").toString();
        const qint64 prekeyId = static_cast<qint64>(bundle.value("prekeyId").toDouble());
        const QByteArray identityPub =
            QByteArray::fromBase64(bundle.value("identityPub").toString().toLatin1());
        const QByteArray prekeyPub =
            QByteArray::fromBase64(bundle.value("prekeyPub").toString().toLatin1());
        if (deviceId.isEmpty() || prekeyId <= 0) {
            return {};
        }

        const auto eph = E2eeCrypto::generateX25519KeyPair();
        if (!eph.valid) {
            return {};
        }

        QByteArray shared = E2eeCrypto::ecdh(eph.privateKey, prekeyPub);
        shared += E2eeCrypto::ecdh(eph.privateKey, identityPub);
        SecureMemory::wipe(const_cast<QByteArray &>(eph.privateKey));
        if (shared.size() != 64) {
            SecureMemory::wipe(shared);
            return {};
        }

        QByteArray key = E2eeCrypto::deriveMessageKey(shared);
        SecureMemory::wipe(shared);
        if (key.isEmpty()) {
            return {};
        }

        const auto gcm = E2eeCrypto::aesGcmEncrypt(key, plainBytes);
        SecureMemory::wipe(key);
        if (!gcm.valid) {
            return {};
        }

        E2eeCrypto::EnvelopeEntry entry;
        entry.deviceId = deviceId;
        entry.prekeyId = prekeyId;
        entry.ephemeralPublicKey = eph.publicKey;
        entry.iv = gcm.iv;
        entry.ciphertext = gcm.ciphertext;
        entries.append(entry);
    }

    // 修复：追加发送方自己设备的拷贝（prekeyId=0，仅用本人身份密钥加密，
    // 不消费预密钥），使发送方重新登录/多端同步后仍能解密自己发出的消息
    if (m_identityKey.valid && !m_localDeviceId.isEmpty()) {
        const auto eph = E2eeCrypto::generateX25519KeyPair();
        if (eph.valid) {
            const QByteArray dh = E2eeCrypto::ecdh(eph.privateKey, m_identityKey.publicKey);
            SecureMemory::wipe(const_cast<QByteArray &>(eph.privateKey));
            if (dh.size() == 32) {
                QByteArray shared = dh + dh;
                SecureMemory::wipe(const_cast<QByteArray &>(dh));
                QByteArray key = E2eeCrypto::deriveMessageKey(shared);
                SecureMemory::wipe(shared);
                const auto gcm = E2eeCrypto::aesGcmEncrypt(key, plainBytes);
                SecureMemory::wipe(key);
                if (gcm.valid) {
                    E2eeCrypto::EnvelopeEntry selfEntry;
                    selfEntry.deviceId = m_localDeviceId;
                    selfEntry.prekeyId = E2eeCrypto::SelfCopyPrekeyId;
                    selfEntry.ephemeralPublicKey = eph.publicKey;
                    selfEntry.iv = gcm.iv;
                    selfEntry.ciphertext = gcm.ciphertext;
                    entries.append(selfEntry);
                }
            }
        }
    }

    if (entries.isEmpty()) {
        return {};
    }
    return QString::fromUtf8(
        QJsonDocument(E2eeCrypto::encodeEnvelope(entries)).toJson(QJsonDocument::Compact));
}

// 解密接收正文：非 envelope（M6 前存量明文）原样返回；否则逐本地预密钥尝试解密
QString NetworkManager::decryptIncomingContent(const QString &content, bool *undecryptable)
{
    using namespace XYChat::Security;

    if (undecryptable) {
        *undecryptable = false;
    }
    if (!E2eeCrypto::looksLikeEnvelope(content)) {
        return content; // 存量明文消息
    }

    bool ok = false;
    const auto entries = E2eeCrypto::decodeEnvelope(content, &ok);
    if (!ok) {
        if (undecryptable) {
            *undecryptable = true;
        }
        return {};
    }

    // 找到属于本设备的条目并尝试解密（同一台机器上收发双方 deviceId 可能
    // 相同，自身拷贝与接收方条目都要尝试，失败继续下一条）
    for (const auto &entry : entries) {
        if (entry.deviceId != m_localDeviceId) {
            continue;
        }
        if (!m_identityKey.valid) {
            continue;
        }

        if (entry.prekeyId == E2eeCrypto::SelfCopyPrekeyId) {
            // 自己设备的拷贝：仅用身份密钥解密，不消费预密钥
            QByteArray dh = E2eeCrypto::ecdh(m_identityKey.privateKey, entry.ephemeralPublicKey);
            if (dh.size() != 32) {
                continue;
            }
            QByteArray shared = dh + dh;
            SecureMemory::wipe(dh);
            QByteArray key = E2eeCrypto::deriveMessageKey(shared);
            SecureMemory::wipe(shared);
            const QByteArray plain = E2eeCrypto::aesGcmDecrypt(key, entry.iv, entry.ciphertext);
            SecureMemory::wipe(key);
            if (!plain.isEmpty()) {
                return QString::fromUtf8(plain);
            }
            continue;
        }

        // 服务端预密钥 ID 本地未知：逐个本地预密钥尝试，GCM 认证标签验证正确性
        for (int i = 0; i < m_localPrekeys.size(); ++i) {
            QByteArray shared = E2eeCrypto::ecdh(m_localPrekeys.at(i).privateKey,
                                                 entry.ephemeralPublicKey);
            shared += E2eeCrypto::ecdh(m_identityKey.privateKey, entry.ephemeralPublicKey);
            QByteArray key = E2eeCrypto::deriveMessageKey(shared);
            SecureMemory::wipe(shared);
            const QByteArray plain = E2eeCrypto::aesGcmDecrypt(key, entry.iv, entry.ciphertext);
            SecureMemory::wipe(key);
            if (!plain.isEmpty()) {
                // 一次性预密钥已消费：从本地删除（前向安全）；
                // 同消息的后续重复投递/重新同步由持久化解密缓存兜底
                m_localPrekeys.removeAt(i);
                KeyStorage::savePrekeys(m_username, m_localDeviceId, m_localPrekeys);
                return QString::fromUtf8(plain);
            }
        }
    }

    if (undecryptable) {
        *undecryptable = true;
    }
    return {};
}

// 在接收消息 JSON 上就地解密 content；失败时标记 undecryptable
void NetworkManager::decryptMessageObject(QJsonObject &msg)
{
    const qint64 msgId = msg.value("messageId").toVariant().toLongLong();

    // 同一消息可能经通知与同步重复投递：命中缓存避免重复消费预密钥
    if (msgId > 0 && m_decryptCache.contains(msgId)) {
        msg["content"] = m_decryptCache.value(msgId);
        return;
    }
    // M6.5: LocalStore 持久化解密缓存兜底（预密钥已消费后重新同步仍可恢复明文）
    if (msgId > 0) {
        const QString cached = m_localStore.loadDecryptedContent(msgId);
        if (!cached.isEmpty()) {
            msg["content"] = cached;
            m_decryptCache.insert(msgId, cached);
            return;
        }
    }

    const QString content = msg.value("content").toString();

    // M7b: 群聊 Sender Key 分发消息
    if (GroupE2eeCrypto::looksLikeDistribution(content)) {
        processGroupSenderKeyDistribution(msg);
        msg["content"] = QStringLiteral("[Sender key updated]");
        msg["contentType"] = QStringLiteral("system");
        return;
    }

    // M7b: 群聊 E2EE 消息
    if (GroupE2eeCrypto::looksLikeGroupMessage(content)) {
        decryptGroupMessageObject(msg);
        return;
    }

    bool undecryptable = false;
    const QString plain = decryptIncomingContent(content, &undecryptable);
    if (undecryptable) {
        msg["undecryptable"] = true;
    } else {
        msg["content"] = plain;
        if (msgId > 0) {
            m_decryptCache.insert(msgId, plain);
            if (m_decryptCache.size() > 2000) {
                m_decryptCache.clear();
            }
            // M6.5: 解密缓存归口 LocalStore（加密存储）；本地库不可用时回退
            // KeyStorage（DPAPI 保护），避免预密钥消费后明文不可恢复
            if (!m_localStore.saveDecryptedContent(msgId, plain)) {
                KeyStorage::saveDecryptCache(m_username, m_localDeviceId, m_decryptCache);
            }
        }
    }
}

// M7b: 确保本机在该群有 Sender Key；返回 true 表示 key 已可用
bool NetworkManager::ensureGroupSenderKey(qint64 conversationId,
                                          GroupE2eeCrypto::SenderKey &key)
{
    key = GroupE2eeCrypto::SenderKey{};
    if (conversationId <= 0 || m_userId <= 0 || m_localDeviceId.isEmpty()) {
        return false;
    }

    auto it = m_groupSenderKeys.find(conversationId);
    if (it != m_groupSenderKeys.end() && it->valid) {
        key = it.value();
        return true;
    }

    if (m_localStore.isOpen()) {
        const QString keyId = m_localStore.latestSenderKeyId(conversationId, m_userId, m_localDeviceId);
        if (!keyId.isEmpty()) {
            QByteArray chainKey;
            QByteArray publicSigningKey;
            QByteArray privateSigningKey;
            int iteration = 0;
            if (m_localStore.loadSenderKey(conversationId, m_userId, m_localDeviceId,
                                           keyId, chainKey, publicSigningKey,
                                           privateSigningKey, iteration)) {
                GroupE2eeCrypto::SenderKey stored;
                stored.keyId = keyId;
                stored.chainKey = chainKey;
                stored.publicSigningKey = publicSigningKey;
                stored.privateSigningKey = privateSigningKey;
                stored.iteration = iteration;
                stored.valid = !keyId.isEmpty() && chainKey.size() == 32
                    && publicSigningKey.size() == 32 && privateSigningKey.size() == 32;
                if (stored.valid) {
                    m_groupSenderKeys.insert(conversationId, stored);
                    key = stored;
                    return true;
                }
            }
        }
    }
    key = GroupE2eeCrypto::SenderKey{};
    return false;
}

void NetworkManager::sendFetchGroupKeysRequest(qint64 conversationId)
{
    if (m_state != ConnectionState::Authenticated || conversationId <= 0) {
        return;
    }
    QJsonObject json;
    json["type"] = "fetch_group_keys";
    json["conversationId"] = conversationId;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::FetchGroupKeysRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingFetchGroupKeysRequestId = packet.requestId;
    m_fetchGroupKeysTargetConvId = conversationId;
    sendPacket(packet);
}

void NetworkManager::handleFetchGroupKeysResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingFetchGroupKeysRequestId) {
        return;
    }
    const qint64 convId = m_fetchGroupKeysTargetConvId;
    m_pendingFetchGroupKeysRequestId = 0;
    m_fetchGroupKeysTargetConvId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    const int code = response.value("code").toInt();
    if (code != static_cast<int>(ErrorCode::Ok) || convId <= 0) {
        qWarning() << "[NetMgr] FetchGroupKeys failed:" << response.value("message").toString();
        // 保持 outbox，稍后由重连或下一条消息触发重试
        return;
    }

    const QJsonObject data = response.value("data").toObject();
    const QJsonObject bundlesByUser = data.value("bundles").toObject();
    const QString distribution = buildGroupSenderKeyDistribution(convId, bundlesByUser);
    if (distribution.isEmpty()) {
        qWarning() << "[NetMgr] Failed to build group sender key distribution for" << convId;
        return;
    }

    const QString clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject json;
    json["type"] = "send_message";
    json["conversationId"] = convId;
    json["content"] = distribution;
    json["contentType"] = "sender_key_distribution";
    json["clientMessageId"] = clientMessageId;
    addReplayProtection(json);

    Packet distPacket;
    distPacket.messageType = MessageType::SendMessageRequest;
    distPacket.requestId = nextRequestId();
    distPacket.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingSendByRequestId.insert(distPacket.requestId, clientMessageId);
    m_pendingGroupDistributions.insert(clientMessageId);
    sendPacket(distPacket);
}

QString NetworkManager::buildGroupSenderKeyDistribution(qint64 conversationId,
                                                        const QJsonObject &bundlesByUser)
{
    if (conversationId <= 0 || bundlesByUser.isEmpty()) {
        return {};
    }

    GroupE2eeCrypto::SenderKey key = GroupE2eeCrypto::generateSenderKey();
    if (!key.valid) {
        return {};
    }

    QList<GroupE2eeCrypto::DistributionEntry> entries;
    const QString chainKeyB64 = QString::fromLatin1(key.chainKey.toBase64());

    for (auto it = bundlesByUser.constBegin(); it != bundlesByUser.constEnd(); ++it) {
        const qint64 userId = it.key().toLongLong();
        if (userId <= 0) {
            continue;
        }
        const QJsonArray bundles = it.value().toArray();
        if (bundles.isEmpty()) {
            continue;
        }
        // 复用 pairwise E2EE 加密 chainKey，然后解析出各设备条目
        const QString envelope = encryptForUser(userId, bundles, chainKeyB64);
        if (envelope.isEmpty()) {
            continue;
        }
        bool ok = false;
        const auto deviceEntries = E2eeCrypto::decodeEnvelope(envelope, &ok);
        if (!ok) {
            continue;
        }
        for (const auto &entry : deviceEntries) {
            GroupE2eeCrypto::DistributionEntry distEntry;
            distEntry.userId = userId;
            distEntry.deviceId = entry.deviceId;
            distEntry.envelope = entry;
            entries.append(distEntry);
        }
    }

    if (entries.isEmpty()) {
        SecureMemory::wipe(key.chainKey);
        SecureMemory::wipe(key.privateSigningKey);
        return {};
    }

    // 保存发送方 Sender Key（chainKey 与私钥经 LocalStore 存储密钥加密）
    if (m_localStore.isOpen()) {
        m_localStore.saveSenderKey(conversationId, m_userId, m_localDeviceId,
                                   key.keyId, key.chainKey,
                                   key.publicSigningKey, key.privateSigningKey,
                                   key.iteration);
    }
    m_groupSenderKeys.insert(conversationId, key);

    const QJsonObject distJson = GroupE2eeCrypto::encodeDistribution(
        conversationId, m_userId, m_localDeviceId, key, entries);
    return QString::fromUtf8(QJsonDocument(distJson).toJson(QJsonDocument::Compact));
}

// 解密单个 pairwise envelope 条目（用于提取 group Sender Key 的 chainKey）
static QByteArray decryptEnvelopeEntry(const XYChat::Security::E2eeCrypto::EnvelopeEntry &entry,
                                       const XYChat::Security::E2eeCrypto::KeyPair &identityKey,
                                       QList<KeyStorage::PrekeyEntry> &localPrekeys,
                                       const QString &username,
                                       const QString &deviceId)
{
    using namespace XYChat::Security;
    if (!identityKey.valid || entry.deviceId != deviceId) {
        return {};
    }

    if (entry.prekeyId == E2eeCrypto::SelfCopyPrekeyId) {
        QByteArray dh = E2eeCrypto::ecdh(identityKey.privateKey, entry.ephemeralPublicKey);
        if (dh.size() != 32) {
            return {};
        }
        QByteArray shared = dh + dh;
        SecureMemory::wipe(dh);
        QByteArray key = E2eeCrypto::deriveMessageKey(shared);
        SecureMemory::wipe(shared);
        const QByteArray plain = E2eeCrypto::aesGcmDecrypt(key, entry.iv, entry.ciphertext);
        SecureMemory::wipe(key);
        return plain;
    }

    for (int i = 0; i < localPrekeys.size(); ++i) {
        QByteArray shared = E2eeCrypto::ecdh(localPrekeys.at(i).privateKey,
                                             entry.ephemeralPublicKey);
        shared += E2eeCrypto::ecdh(identityKey.privateKey, entry.ephemeralPublicKey);
        QByteArray key = E2eeCrypto::deriveMessageKey(shared);
        SecureMemory::wipe(shared);
        const QByteArray plain = E2eeCrypto::aesGcmDecrypt(key, entry.iv, entry.ciphertext);
        SecureMemory::wipe(key);
        if (!plain.isEmpty()) {
            localPrekeys.removeAt(i);
            KeyStorage::savePrekeys(username, deviceId, localPrekeys);
            return plain;
        }
    }
    return {};
}

bool NetworkManager::processGroupSenderKeyDistribution(const QJsonObject &msg)
{
    const qint64 convId = msg.value("conversationId").toVariant().toLongLong();
    const QString content = msg.value("content").toString();
    if (convId <= 0 || content.isEmpty()) {
        return false;
    }

    qint64 groupId = 0;
    qint64 senderUserId = 0;
    QString senderDeviceId;
    GroupE2eeCrypto::SenderKey key;
    QList<GroupE2eeCrypto::DistributionEntry> entries;
    if (!GroupE2eeCrypto::decodeDistribution(content, groupId, senderUserId,
                                             senderDeviceId, key, entries)) {
        return false;
    }

    if (groupId != convId || senderUserId <= 0 || senderDeviceId.isEmpty()) {
        return false;
    }

    QByteArray decryptedChainKeyB64;
    for (const auto &entry : entries) {
        if (entry.userId == m_userId && entry.deviceId == m_localDeviceId) {
            decryptedChainKeyB64 = decryptEnvelopeEntry(entry.envelope, m_identityKey, m_localPrekeys,
                                                        m_username, m_localDeviceId);
            break;
        }
    }
    if (decryptedChainKeyB64.isEmpty()) {
        qWarning() << "[NetMgr] Failed to decrypt sender key distribution for group" << convId;
        return false;
    }

    const QByteArray chainKey = QByteArray::fromBase64(decryptedChainKeyB64,
                                                       QByteArray::AbortOnBase64DecodingErrors);
    if (chainKey.size() != 32) {
        qWarning() << "[NetMgr] Decoded sender key has wrong length" << chainKey.size()
                   << "for group" << convId;
        return false;
    }

    if (m_localStore.isOpen()) {
        m_localStore.saveSenderKey(convId, senderUserId, senderDeviceId, key.keyId,
                                   chainKey, key.publicSigningKey,
                                   QByteArray(), key.iteration);
    }
    qInfo() << "[NetMgr] Installed group sender key for group" << convId
            << "from user" << senderUserId << "keyId" << key.keyId;
    return true;
}

QString NetworkManager::encryptGroupMessage(qint64 conversationId, const QString &plaintext)
{
    GroupE2eeCrypto::SenderKey key;
    if (!ensureGroupSenderKey(conversationId, key) || !key.valid) {
        return {};
    }

    // 从内存缓存中取出可修改的副本（ratchet 会修改 chainKey/iteration）
    GroupE2eeCrypto::SenderKey mutableKey = m_groupSenderKeys.value(conversationId);
    if (!mutableKey.valid) {
        return {};
    }

    const auto encrypted = GroupE2eeCrypto::encryptMessage(mutableKey, plaintext.toUtf8());
    if (!encrypted.valid) {
        return {};
    }

    // 保存 ratchet 后的状态
    if (m_localStore.isOpen()) {
        m_localStore.saveSenderKey(conversationId, m_userId, m_localDeviceId,
                                   mutableKey.keyId, mutableKey.chainKey,
                                   mutableKey.publicSigningKey,
                                   mutableKey.privateSigningKey, mutableKey.iteration);
    }
    m_groupSenderKeys.insert(conversationId, mutableKey);

    const QJsonObject envelope = GroupE2eeCrypto::encodeGroupMessage(encrypted, m_localDeviceId);
    return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

bool NetworkManager::decryptGroupMessageObject(QJsonObject &msg)
{
    const qint64 msgId = msg.value("messageId").toVariant().toLongLong();
    const qint64 convId = msg.value("conversationId").toVariant().toLongLong();
    const QString content = msg.value("content").toString();

    GroupE2eeCrypto::EncryptedMessage encrypted;
    QString senderDeviceId;
    if (!GroupE2eeCrypto::decodeGroupMessage(content, encrypted, &senderDeviceId)) {
        return false;
    }

    const qint64 senderUserId = msg.value("senderId").toVariant().toLongLong();

    QByteArray chainKey;
    QByteArray publicSigningKey;
    QByteArray privateSigningKey;
    int iteration = 0;
    bool loaded = false;
    if (m_localStore.isOpen()) {
        loaded = m_localStore.loadSenderKey(convId, senderUserId, senderDeviceId,
                                            encrypted.keyId, chainKey, publicSigningKey,
                                            privateSigningKey, iteration);
    }
    if (!loaded) {
        qWarning() << "[NetMgr] No sender key for group" << convId
                   << "sender" << senderUserId << "device" << senderDeviceId
                   << "keyId" << encrypted.keyId;
        msg["content"] = QString();
        msg["undecryptable"] = true;
        return false;
    }

    const QByteArray plain = GroupE2eeCrypto::decryptMessage(chainKey, iteration,
                                                             publicSigningKey, encrypted);
    if (plain.isEmpty()) {
        qWarning() << "[NetMgr] Group message decryption failed for group" << convId
                   << "sender" << senderUserId << "keyId" << encrypted.keyId
                   << "iteration" << encrypted.iteration;
        msg["content"] = QString();
        msg["undecryptable"] = true;
        return false;
    }

    msg["content"] = QString::fromUtf8(plain);
    if (msgId > 0) {
        m_decryptCache.insert(msgId, QString::fromUtf8(plain));
        if (m_decryptCache.size() > 2000) {
            m_decryptCache.clear();
        }
        if (!m_localStore.saveDecryptedContent(msgId, QString::fromUtf8(plain))) {
            KeyStorage::saveDecryptCache(m_username, m_localDeviceId, m_decryptCache);
        }
    }

    // 保存 ratchet 后的 chainKey/iteration
    m_localStore.saveSenderKey(convId, senderUserId, senderDeviceId, encrypted.keyId,
                               chainKey, publicSigningKey, QByteArray(), iteration);
    return true;
}

// M3: 确认消息
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

// M3: 同步消息
void NetworkManager::syncMessages(qint64 conversationId, qint64 afterId, int limit)
{
    // M6.5: 首页拉取先立即展示本地缓存（重启后即刻可见、离线可查），
    // 随后服务端响应到达时以权威数据覆盖
    if (afterId == 0 && conversationId > 0) {
        const QJsonArray cached = m_localStore.loadMessages(conversationId, limit);
        if (!cached.isEmpty()) {
            emit messagesSynced(conversationId, cached, false);
        }
    }

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

// M3: 响应处理
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
        // M6: 会话预览中的 envelope 密文替换为占位文本
        QJsonArray conversations =
            response.value("data").toObject().value("conversations").toArray();
        for (QJsonValueRef value : conversations) {
            QJsonObject conv = value.toObject();
            const QString lastMessage = conv.value("lastMessage").toString();
            if (XYChat::Security::E2eeCrypto::looksLikeEnvelope(lastMessage)) {
                conv["lastMessage"] = "[Encrypted message]";
                value = conv;
                continue;
            }
            // M7a: 群系统消息预览（结构化 JSON）转为可读摘要
            const QJsonObject sysObj = QJsonDocument::fromJson(lastMessage.toUtf8()).object();
            if (!sysObj.isEmpty() && sysObj.contains("event")) {
                conv["lastMessage"] = systemMessageSummary(lastMessage);
                value = conv;
            }
        }
        // M6.5: 服务端权威会话数据写入本地缓存；预览为占位符时先用本地
        // 解密缓存回填真实明文，避免持久化预览退化为 "[Encrypted message]"
        for (QJsonValueRef value : conversations) {
            QJsonObject conv = value.toObject();
            if (conv.value("lastMessage").toString() == "[Encrypted message]") {
                const qint64 lastMessageId =
                    conv.value("lastMessageId").toVariant().toLongLong();
                const QString cached = m_localStore.loadDecryptedContent(lastMessageId);
                if (!cached.isEmpty()) {
                    conv["lastMessage"] = cached;
                    value = conv;
                }
            }
            m_localStore.upsertConversation(conv);
        }
        emit conversationsResult(conversations);
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

        // M7b: 群聊 Sender Key 分发消息 ACK：不展示、不落库，触发后续群消息发送
        if (m_pendingGroupDistributions.remove(ackedId)) {
            for (int i = m_outbox.size() - 1; i >= 0; --i) {
                if (m_outbox.at(i).clientMessageId == ackedId) {
                    m_localStore.removeOutboxItem(ackedId);
                    m_outbox.removeAt(i);
                    break;
                }
            }
            flushOutbox();
            return;
        }

        QString sentContent;
        for (int i = 0; i < m_outbox.size(); ++i) {
            if (m_outbox.at(i).clientMessageId == ackedId) {
                sentContent = m_outbox.at(i).content;
                m_outbox.removeAt(i);
                break;
            }
        }
        // M6.5: 同步移除持久化 outbox，并将已发送消息写入本地缓存
        m_localStore.removeOutboxItem(ackedId);
        const qint64 messageId = data.value("messageId").toVariant().toLongLong();
        const qint64 conversationId = data.value("conversationId").toVariant().toLongLong();
        if (messageId > 0 && conversationId > 0 && !sentContent.isEmpty()) {
            QJsonObject cached;
            cached["messageId"] = messageId;
            cached["conversationId"] = conversationId;
            cached["senderId"] = m_userId;
            cached["senderUsername"] = m_username;
            cached["content"] = sentContent;
            cached["contentType"] = "text";
            cached["status"] = "sent";
            cached["clientMessageId"] = ackedId;
            cached["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            m_localStore.upsertMessage(cached);
        }
        emit messageSent(messageId, conversationId, ackedId);
    } else {
        // M7a: 群消息的确定性失败（已不在群/会话不存在/请求非法）移除
        // outbox 项避免无限重试；瞬时错误保留重试
        const int code2 = response.value("code").toInt();
        const bool deterministic = code2 == static_cast<int>(ErrorCode::PermissionDenied)
            || code2 == static_cast<int>(ErrorCode::ConversationNotFound)
            || code2 == static_cast<int>(ErrorCode::InvalidRequest);
        if (deterministic) {
            for (int i = m_outbox.size() - 1; i >= 0; --i) {
                if (m_outbox.at(i).clientMessageId == clientMessageId) {
                    if (m_outbox.at(i).conversationId > 0) {
                        m_localStore.removeOutboxItem(clientMessageId);
                        m_outbox.removeAt(i);
                    }
                    break;
                }
            }
        }
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
        // M6: 逐条解密同步到的消息正文
        QJsonArray messages = data.value("messages").toArray();
        QJsonArray visibleMessages;
        for (QJsonValueRef value : messages) {
            QJsonObject msg = value.toObject();
            const QString ct = msg.value("contentType").toString();
            const QString c = msg.value("content").toString();
            // M7b: 群聊 Sender Key 分发消息只处理、不展示、不落库
            if (ct == QLatin1String("sender_key_distribution")
                || GroupE2eeCrypto::looksLikeDistribution(c)) {
                processGroupSenderKeyDistribution(msg);
                continue;
            }
            decryptMessageObject(msg);
            // M6.5: 写入本地缓存（加密存储）
            m_localStore.upsertMessage(msg);
            visibleMessages.append(msg);
        }
        emit messagesSynced(
            data.value("conversationId").toVariant().toLongLong(),
            messages,
            data.value("hasMore").toBool());
    }
}

void NetworkManager::handleNewMessageNotification(const Packet &packet)
{
    QJsonObject msg = QJsonDocument::fromJson(packet.payload).object();

    // M7b: 群聊 Sender Key 分发消息只处理、不展示、不落库
    const QString contentType = msg.value("contentType").toString();
    const QString content = msg.value("content").toString();
    if (contentType == QLatin1String("sender_key_distribution")
        || GroupE2eeCrypto::looksLikeDistribution(content)) {
        processGroupSenderKeyDistribution(msg);
        return;
    }

    // M6: 实时推送的消息先解密再交给 UI
    decryptMessageObject(msg);
    // M6.5: 新消息写入本地缓存，并更新会话预览/未读数（仅更新已存在会话）
    m_localStore.upsertMessage(msg);
    {
        const qint64 convId = msg.value("conversationId").toVariant().toLongLong();
        // M7a: 群系统消息预览用可读摘要，避免结构化 JSON 直接展示
        const bool isSystem = msg.value("contentType").toString() == "system";
        const QString preview = msg.value("undecryptable").toBool()
            ? "[Encrypted message]"
            : (isSystem ? systemMessageSummary(msg.value("content").toString())
                        : msg.value("content").toString());
        m_localStore.bumpConversationPreview(convId, preview, true);
    }
    emit newMessageReceived(msg);

    // 自动发送已送达确认
    const qint64 msgId = msg.value("messageId").toVariant().toLongLong();
    if (msgId > 0) {
        ackMessage(msgId, "delivered");
    }
}

// M5.5: 消息状态更新推送
void NetworkManager::handleMessageStatusUpdate(const Packet &packet)
{
    const QJsonObject msg = QJsonDocument::fromJson(packet.payload).object();
    const qint64 msgId = msg.value("messageId").toVariant().toLongLong();
    const QString status = msg.value("status").toString();
    if (msgId > 0 && !status.isEmpty()) {
        // M6.5: 同步更新本地缓存中的消息状态
        m_localStore.updateMessageStatus(msgId, status);
        emit messageStatusChanged(msgId, status);
    }
}

// M5.5: 账号级增量同步
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
        // M6: 解密事件流中的 message 事件正文
        QJsonArray events = data.value("events").toArray();
        for (QJsonValueRef value : events) {
            QJsonObject event = value.toObject();
            if (event.value("type").toString() == "message") {
                QJsonObject payload = event.value("payload").toObject();
                const QString ct = payload.value("contentType").toString();
                const QString c = payload.value("content").toString();
                // M7b: 分发消息直接处理，不需要解密展示
                if (ct == QLatin1String("sender_key_distribution")
                    || GroupE2eeCrypto::looksLikeDistribution(c)) {
                    processGroupSenderKeyDistribution(payload);
                } else {
                    decryptMessageObject(payload);
                }
                event["payload"] = payload;
                value = event;
            }
        }
        // M6.5: 事件写入本地缓存并推进游标（hasMore 时自动续拉）
        const qint64 lastSeq = data.value("lastSeq").toVariant().toLongLong();
        const bool hasMore = data.value("hasMore").toBool();
        ingestSyncEvents(events, lastSeq, hasMore);
        emit eventsSynced(events, lastSeq, hasMore);
    }
}

// M6.5: 本地持久化缓存
void NetworkManager::openLocalStore()
{
    if (m_username.isEmpty() || m_localDeviceId.isEmpty()) {
        return;
    }
    // 切换账号：清除旧账号用户可见数据后关闭（保留其解密缓存，
    // 该账号重登时仍需依靠它解密），再打开新账号本地库
    if (m_localStore.isOpen() && m_localStore.username() != m_username) {
        m_localStore.clearUserData();
        m_localStore.close();
    }
    if (!m_localStore.isOpen() && !m_localStore.open(m_username, m_localDeviceId)) {
        qWarning() << "[NetMgr] LocalStore unavailable, running without local cache";
        return;
    }

    // M6 遗留解密缓存一次性迁入 LocalStore（幂等，无文件时为空操作）
    m_localStore.importLegacyDecryptCache(m_username, m_localDeviceId);

    // 持久化 outbox 载入内存（幂等键去重），由后续 flushOutbox 重发
    const QList<LocalStore::OutboxItem> persisted = m_localStore.loadOutbox();
    for (const LocalStore::OutboxItem &item : persisted) {
        bool exists = false;
        for (const OutboxItem &mem : std::as_const(m_outbox)) {
            if (mem.clientMessageId == item.clientMessageId) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_outbox.append({item.clientMessageId, item.toUserId,
                             item.conversationId, item.content});
        }
    }

    emitCachedConversations();

    // 基于 sync_events 游标增量同步，补齐离线期间错过的消息/回执
    syncEvents(m_localStore.syncCursor());
}

void NetworkManager::emitCachedConversations()
{
    if (!m_localStore.isOpen()) {
        return;
    }
    const QJsonArray cached = m_localStore.loadConversations();
    if (!cached.isEmpty()) {
        // 立即展示上一会话周期的会话列表，随后服务端数据到达时刷新
        emit conversationsResult(cached);
    }
}

void NetworkManager::ingestSyncEvents(const QJsonArray &events, qint64 lastSeq, bool hasMore)
{
    if (!m_localStore.isOpen()) {
        return;
    }

    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        const QString type = event.value("type").toString();
        const QJsonObject payload = event.value("payload").toObject();

        if (type == "message") {
            QJsonObject msg = payload;
            // M7b: 分发消息不入库
            const QString ct = msg.value("contentType").toString();
            const QString c = msg.value("content").toString();
            if (ct == QLatin1String("sender_key_distribution")
                || GroupE2eeCrypto::looksLikeDistribution(c)) {
                processGroupSenderKeyDistribution(msg);
                continue;
            }
            // 事件流无状态字段：按发送方推导初始状态
            const bool fromSelf = msg.value("senderId").toVariant().toLongLong() == m_userId;
            msg["status"] = fromSelf ? "sent"
                                     : "delivered";
            // 本机已成功投递的消息同步移除 outbox（ACK 丢失时的兜底）
            const QString cmid = msg.value("clientMessageId").toString();
            if (fromSelf && !cmid.isEmpty()) {
                for (int i = m_outbox.size() - 1; i >= 0; --i) {
                    if (m_outbox.at(i).clientMessageId == cmid) {
                        m_outbox.removeAt(i);
                        break;
                    }
                }
                m_localStore.removeOutboxItem(cmid);
            }
            m_localStore.upsertMessage(msg);
        } else if (type == "receipt") {
            m_localStore.updateMessageStatus(
                payload.value("messageId").toVariant().toLongLong(),
                payload.value("status").toString());
        }
        // contact_added 等事件忽略：联系人列表按需从服务端拉取
    }

    // 游标单调前进
    if (lastSeq > m_localStore.syncCursor()) {
        m_localStore.setSyncCursor(lastSeq);
    }
    // 仍有事件时继续增量拉取，直至追平
    if (hasMore && m_state == ConnectionState::Authenticated) {
        syncEvents(lastSeq);
    }
}

// QML 辅助方法
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

// M7a: 群组操作

void NetworkManager::createGroup(const QString &name, const QVariantList &memberIds)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "create_group";
    json["name"] = name;
    QJsonArray members;
    for (const QVariant &v : memberIds) {
        const qint64 id = v.toLongLong();
        if (id > 0 && id != m_userId) {
            members.append(id);
        }
    }
    json["memberIds"] = members;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::CreateGroupRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingCreateGroupRequestId = packet.requestId;
    sendPacket(packet);
}

void NetworkManager::inviteGroupMembers(qint64 conversationId, const QVariantList &userIds)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "invite_group_members";
    json["conversationId"] = conversationId;
    QJsonArray users;
    for (const QVariant &v : userIds) {
        const qint64 id = v.toLongLong();
        if (id > 0 && id != m_userId) {
            users.append(id);
        }
    }
    json["userIds"] = users;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::InviteGroupMembersRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingInviteGroupRequestId = packet.requestId;
    sendPacket(packet);
}

void NetworkManager::leaveGroup(qint64 conversationId)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "leave_group";
    json["conversationId"] = conversationId;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::LeaveGroupRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingLeaveGroupRequestId = packet.requestId;
    sendPacket(packet);
}

void NetworkManager::kickGroupMember(qint64 conversationId, qint64 userId)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "kick_group_member";
    json["conversationId"] = conversationId;
    json["userId"] = userId;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::KickGroupMemberRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingKickGroupRequestId = packet.requestId;
    sendPacket(packet);
}

void NetworkManager::getGroupInfo(qint64 conversationId)
{
    if (m_state != ConnectionState::Authenticated) return;

    QJsonObject json;
    json["type"] = "get_group_info";
    json["conversationId"] = conversationId;
    addReplayProtection(json);

    Packet packet;
    packet.messageType = MessageType::GetGroupInfoRequest;
    packet.requestId = nextRequestId();
    packet.payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_pendingGetGroupInfoRequestId = packet.requestId;
    sendPacket(packet);
}

// M7a: 群组响应

void NetworkManager::handleCreateGroupResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingCreateGroupRequestId) return;
    m_pendingCreateGroupRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() != static_cast<int>(ErrorCode::Ok)) {
        emit groupRequestFailed(response.value("message").toString("Failed to create group"));
        return;
    }

    const QJsonObject data = response.value("data").toObject();
    const qint64 conversationId = data.value("conversationId").toVariant().toLongLong();
    const QString name = data.value("name").toString();

    // 新群立即写入本地缓存（缓存先行，服务端刷新随后覆盖）
    QJsonObject conv = data;
    conv["type"] = "group";
    conv["unreadCount"] = 0;
    m_localStore.upsertConversation(conv);
    getConversations();

    emit groupCreated(conversationId, name);
}

void NetworkManager::handleInviteGroupMembersResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingInviteGroupRequestId) return;
    m_pendingInviteGroupRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() != static_cast<int>(ErrorCode::Ok)) {
        emit groupRequestFailed(response.value("message").toString("Failed to invite members"));
        return;
    }

    const QJsonObject data = response.value("data").toObject();
    getConversations();
    emit groupMembersInvited(data.value("conversationId").toVariant().toLongLong());
}

void NetworkManager::handleLeaveGroupResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingLeaveGroupRequestId) return;
    m_pendingLeaveGroupRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() != static_cast<int>(ErrorCode::Ok)) {
        emit groupRequestFailed(response.value("message").toString("Failed to leave group"));
        return;
    }

    const qint64 conversationId =
        response.value("data").toObject().value("conversationId").toVariant().toLongLong();
    getConversations();
    emit groupLeft(conversationId);
}

void NetworkManager::handleKickGroupMemberResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingKickGroupRequestId) return;
    m_pendingKickGroupRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() != static_cast<int>(ErrorCode::Ok)) {
        emit groupRequestFailed(response.value("message").toString("Failed to remove member"));
        return;
    }

    const QJsonObject data = response.value("data").toObject();
    emit groupMemberKicked(data.value("conversationId").toVariant().toLongLong(),
                           data.value("removedUserId").toVariant().toLongLong());
}

void NetworkManager::handleGetGroupInfoResponse(const Packet &packet)
{
    if (packet.requestId != m_pendingGetGroupInfoRequestId) return;
    m_pendingGetGroupInfoRequestId = 0;

    const QJsonObject response = QJsonDocument::fromJson(packet.payload).object();
    if (response.value("code").toInt() != static_cast<int>(ErrorCode::Ok)) {
        emit groupRequestFailed(response.value("message").toString("Failed to get group info"));
        return;
    }

    emit groupInfoResult(response.value("data").toObject());
}

// M7a: 群变更推送：刷新会话列表并通知 UI（被移除/退群后群从列表消失）
void NetworkManager::handleGroupChangedNotification(const Packet &packet)
{
    const QJsonObject payload = QJsonDocument::fromJson(packet.payload).object();
    getConversations();
    emit groupChanged(payload);
}

// M7a: 群系统消息摘要（contentType=system 的结构化正文转可读文本）
QString NetworkManager::systemMessageSummary(const QString &content)
{
    const QJsonObject obj = QJsonDocument::fromJson(content.toUtf8()).object();
    const QString event = obj.value("event").toString();
    if (event == "group_created") {
        return "创建了群组";
    }
    if (event == "member_added") {
        return "新成员加入群聊";
    }
    if (event == "member_removed") {
        return "成员被移出群聊";
    }
    if (event == "member_left") {
        return "成员退出了群聊";
    }
    if (event == "owner_transferred") {
        return "群主已转让";
    }
    return content;
}
