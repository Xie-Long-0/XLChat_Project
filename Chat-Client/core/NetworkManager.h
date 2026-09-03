#pragma once

#include <QObject>
#include <QSslSocket>
#include <QSslError>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QHash>
#include <QSet>

#include "protocol/PacketCodec.h"
#include "encryption/E2eeCrypto.h"
#include "encryption/GroupE2eeCrypto.h"
#include "KeyStorage.h"
#include "LocalStore.h"

class NetworkManager : public QObject
{
    Q_OBJECT
    // 暴露给 QML 的只读属性（带 NOTIFY 以支持响应式绑定）
    Q_PROPERTY(ConnectionState state READ state NOTIFY connectionStateChanged)
    Q_PROPERTY(QString sessionToken READ sessionToken NOTIFY sessionChanged)
    Q_PROPERTY(qint64 userId READ userId NOTIFY sessionChanged)
    Q_PROPERTY(QString username READ username NOTIFY sessionChanged)

public:
    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Connected,
        LoggingIn,
        Authenticated
    };
    Q_ENUM(ConnectionState)

    explicit NetworkManager(QObject *parent = nullptr);

    // 认证操作
    Q_INVOKABLE void login(const QString &username, const QString &password);
    Q_INVOKABLE void registerAccount(const QString &username, const QString &password,
                                     const QString &email = {}, const QString &phone = {});
    Q_INVOKABLE void logout();
    Q_INVOKABLE void renewToken();

    // M3: 用户搜索与联系人
    Q_INVOKABLE void searchUsers(const QString &query);
    Q_INVOKABLE void addContact(qint64 userId);
    Q_INVOKABLE void getContacts();

    // M3: 会话与消息
    Q_INVOKABLE void getConversations();
    // M4.5: 返回客户端幂等键 clientMessageId，供 QML 跟踪乐观消息状态
    Q_INVOKABLE QString sendMessage(qint64 toUserId, const QString &content);
    Q_INVOKABLE void ackMessage(qint64 messageId, const QString &status = "delivered");
    Q_INVOKABLE void syncMessages(qint64 conversationId, qint64 afterId = 0, int limit = 100);
    // M5.5: 账号级增量同步
    Q_INVOKABLE void syncEvents(qint64 afterSeq = 0, int limit = 200);

    // M7a: 群组操作（明文群聊）
    Q_INVOKABLE void createGroup(const QString &name, const QVariantList &memberIds);
    Q_INVOKABLE void inviteGroupMembers(qint64 conversationId, const QVariantList &userIds);
    Q_INVOKABLE void leaveGroup(qint64 conversationId);
    Q_INVOKABLE void kickGroupMember(qint64 conversationId, qint64 userId);
    Q_INVOKABLE void getGroupInfo(qint64 conversationId);
    // M7a: 发送群消息（明文，返回幂等键供乐观消息跟踪）
    Q_INVOKABLE QString sendGroupMessage(qint64 conversationId, const QString &content);

    // 状态查询
    ConnectionState state() const { return m_state; }
    QString sessionToken() const { return m_sessionToken; }
    qint64 userId() const { return m_userId; }
    QString username() const { return m_username; }

    // QML 可调用的方法
    Q_INVOKABLE QString encryptPassword(const QString &password) const;
    Q_INVOKABLE QVariantList toVariantList(const QJsonArray &array) const;

signals:
    void loginSuccessful();
    void loginFailed(const QString &errorMessage);
    void registerSuccessful();
    void registerFailed(const QString &errorMessage);
    void logoutFinished();
    void connectionStateChanged(NetworkManager::ConnectionState state);
    void sessionChanged();
    // M3 信号
    void searchUsersResult(const QJsonArray &users);
    void contactsResult(const QJsonArray &contacts);
    void conversationsResult(const QJsonArray &conversations);
    void messageSent(qint64 messageId, qint64 conversationId, const QString &clientMessageId);
    void messageSendFailed(const QString &error);
    void newMessageReceived(const QJsonObject &message);
    void messagesSynced(qint64 conversationId, const QJsonArray &messages, bool hasMore);
    void messageAcked(qint64 messageId);
    // M5.5
    void messageStatusChanged(qint64 messageId, const QString &status);
    void eventsSynced(const QJsonArray &events, qint64 lastSeq, bool hasMore);
    // M6: 对方身份公钥指纹变化（TOFU 告警，不阻塞发送）
    void peerIdentityChanged(qint64 peerUserId);
    // M7a: 群组操作结果与推送
    void groupCreated(qint64 conversationId, const QString &name);
    void groupMembersInvited(qint64 conversationId);
    void groupLeft(qint64 conversationId);
    void groupMemberKicked(qint64 conversationId, qint64 removedUserId);
    void groupInfoResult(const QJsonObject &info);
    void groupRequestFailed(const QString &error);
    void groupChanged(const QJsonObject &payload);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError socketError);
    void onSslErrors(const QList<QSslError> &errors);
    void sendHeartbeat();

private:
    void connectToServer();
    void initTls();
    void sendLoginRequest();
    void sendRegisterRequest();
    void handlePacket(const XYChat::Protocol::Packet &packet);
    void handleLoginResponse(const XYChat::Protocol::Packet &packet);
    void handleRegisterResponse(const XYChat::Protocol::Packet &packet);
    void handleLogoutResponse(const XYChat::Protocol::Packet &packet);
    void handleTokenRenewResponse(const XYChat::Protocol::Packet &packet);
    // P1: 服务端 MessageType::Error 回包（鉴权门/校验失败）：清理在途单发槽位并推进 healing 队列
    void handleErrorResponse(const XYChat::Protocol::Packet &packet);
    // M3 响应处理
    void handleSearchUsersResponse(const XYChat::Protocol::Packet &packet);
    void handleAddContactResponse(const XYChat::Protocol::Packet &packet);
    void handleGetContactsResponse(const XYChat::Protocol::Packet &packet);
    void handleGetConversationsResponse(const XYChat::Protocol::Packet &packet);
    void handleSendMessageResponse(const XYChat::Protocol::Packet &packet);
    void handleAckMessageResponse(const XYChat::Protocol::Packet &packet);
    void handleSyncMessagesResponse(const XYChat::Protocol::Packet &packet);
    void handleNewMessageNotification(const XYChat::Protocol::Packet &packet);
    // M5.5
    void handleMessageStatusUpdate(const XYChat::Protocol::Packet &packet);
    void handleSyncEventsResponse(const XYChat::Protocol::Packet &packet);
    // M7a: 群组响应与推送
    void handleCreateGroupResponse(const XYChat::Protocol::Packet &packet);
    void handleInviteGroupMembersResponse(const XYChat::Protocol::Packet &packet);
    void handleLeaveGroupResponse(const XYChat::Protocol::Packet &packet);
    void handleKickGroupMemberResponse(const XYChat::Protocol::Packet &packet);
    void handleGetGroupInfoResponse(const XYChat::Protocol::Packet &packet);
    void handleGroupChangedNotification(const XYChat::Protocol::Packet &packet);
    // M7a: 群系统消息摘要（contentType=system 的结构化正文转可读文本）
    static QString systemMessageSummary(const QString &content);
    void sendPacket(const XYChat::Protocol::Packet &packet);
    quint64 nextRequestId();
    void setState(ConnectionState state);
    void resetAuthState();
    // M5: 重放保护辅助
    void addReplayProtection(QJsonObject &json);
    // M5.5: outbox 重发
    void flushOutbox();
    // M6: E2EE 引导与密钥交换
    void bootstrapE2ee();
    void sendRegisterKeysRequest();
    void sendFetchKeysRequest(qint64 toUserId);
    void handleRegisterKeysResponse(const XYChat::Protocol::Packet &packet);
    void handleFetchKeysResponse(const XYChat::Protocol::Packet &packet);
    // M7b: 群 E2EE 密钥包拉取
    void handleFetchGroupKeysResponse(const XYChat::Protocol::Packet &packet);
    // M6: 对指定用户加密正文（拉取的密钥包逐设备加密），失败返回空
    QString encryptForUser(qint64 toUserId, const QJsonArray &bundles, const QString &plaintext);
    // M6: 解密接收到的消息正文；非 envelope（存量明文）原样返回；
    // 解密失败返回空并置 undecryptable=true
    QString decryptIncomingContent(const QString &content, bool *undecryptable);
    // M6: 在接收 JSON 上就地解密 content 字段（含预览占位替换）
    void decryptMessageObject(QJsonObject &msg);
    // M7b: 群聊 E2EE  Sender Key 管理
    bool ensureGroupSenderKey(qint64 conversationId,
                              XYChat::Security::GroupE2eeCrypto::SenderKey &key);
    void sendFetchGroupKeysRequest(qint64 conversationId);
    QString buildGroupSenderKeyDistribution(qint64 conversationId,
                                            const QJsonObject &bundlesByUser);
    bool processGroupSenderKeyDistribution(const QJsonObject &msg);
    QString encryptGroupMessage(qint64 conversationId, const QString &plaintext);
    bool decryptGroupMessageObject(QJsonObject &msg);
    // P1-3: 群成员变更触发本端 Sender-Key 轮换与重分发（新成员获得密钥、
    // 被移除成员因轮换失去后续消息解密能力）；单发槽位占用时入队待推进
    void healGroupSenderKey(qint64 conversationId);
    void drainHealQueue(); // P1-3: 单发槽位空闲且就绪时从队列推进一个群的轮换重分发
    // M6.5: 本地持久化缓存
    // 登录后打开本地加密库：加载持久化 outbox、立即展示缓存会话、游标增量同步
    void openLocalStore();
    void emitCachedConversations();
    // 将 sync_events 事件写入本地缓存并推进游标（hasMore 时自动续拉）
    void ingestSyncEvents(const QJsonArray &events, qint64 lastSeq, bool hasMore);

private:
    QSslSocket *m_sslSocket;
    QTimer *m_heartbeatTimer;
    QTimer *m_reconnectTimer;
    XYChat::Protocol::PacketCodec m_codec;
    ConnectionState m_state = ConnectionState::Disconnected;
    quint64 m_nextRequestId = 1;
    bool m_tlsEnabled = false;

    // 登录/注册待处理
    quint64 m_pendingLoginRequestId = 0;
    quint64 m_pendingRegisterRequestId = 0;
    QString m_pendingUsername;
    QString m_pendingPassword;
    QString m_pendingRegisterPassword;
    QString m_pendingEmail;
    QString m_pendingPhone;
    bool m_loginQueued = false;
    bool m_registerQueued = false;
    bool m_reconnectEnabled = false;

    // 认证后状态
    QString m_sessionToken;
    qint64 m_userId = 0;
    QString m_username;

    // M3: 待处理请求 ID
    quint64 m_pendingSearchRequestId = 0;
    quint64 m_pendingAddContactRequestId = 0;
    quint64 m_pendingGetContactsRequestId = 0;
    quint64 m_pendingGetConversationsRequestId = 0;
    quint64 m_pendingAckMessageRequestId = 0;
    quint64 m_pendingSyncMessagesRequestId = 0;

    // M5.5: TLS fail-closed 标记（CA 缺失且未显式允许明文时拒绝连接）
    bool m_tlsUnavailable = false;
    quint64 m_pendingSyncEventsRequestId = 0;

    // M7a: 群组请求待处理 ID
    quint64 m_pendingCreateGroupRequestId = 0;
    quint64 m_pendingInviteGroupRequestId = 0;
    quint64 m_pendingLeaveGroupRequestId = 0;
    quint64 m_pendingKickGroupRequestId = 0;
    quint64 m_pendingGetGroupInfoRequestId = 0;
    // M7b: 群 E2EE 引导状态
    quint64 m_pendingFetchGroupKeysRequestId = 0;
    qint64 m_fetchGroupKeysTargetConvId = 0;
    QSet<QString> m_pendingGroupDistributions; // clientMessageId 集合：等待 ACK 的分发消息
    QHash<qint64, XYChat::Security::GroupE2eeCrypto::SenderKey> m_groupSenderKeys; // 内存缓存
    QSet<qint64> m_healQueue; // P1-3: 待轮换重分发的群（单发槽位占用时排队）

    // M5.5: 发送幂等与离线 outbox
    struct OutboxItem
    {
        QString clientMessageId;
        qint64 toUserId = 0;       // 私聊目标（群消息为 0）
        qint64 conversationId = 0; // M7a: 群聊目标会话（私聊为 0）
        QString content;
    };
    QList<OutboxItem> m_outbox;
    QHash<quint64, QString> m_pendingSendByRequestId; // requestId -> clientMessageId

    // M6: E2EE 状态
    QString m_localDeviceId;                          // 登录时使用的 deviceId
    XYChat::Security::E2eeCrypto::KeyPair m_identityKey; // 本机身份密钥对
    QList<KeyStorage::PrekeyEntry> m_localPrekeys;    // 本地未消费的一次性预密钥
    bool m_e2eeReady = false;                         // 身份密钥已注册到服务端
    bool m_e2eeBootstrapPending = false;
    quint64 m_pendingRegisterKeysRequestId = 0;
    quint64 m_pendingFetchKeysRequestId = 0;
    qint64 m_fetchKeysTargetUserId = 0;               // 在途 FetchKeys 的目标用户
    QHash<qint64, qint64> m_fetchBackoffUntil;        // userId -> 重试等待截止时间（秒），避免对未注册密钥的目标空转拉取
    int m_serverPrekeyRemaining = -1;                 // 服务端报告的未认领预密钥余量
    QHash<qint64, QString> m_decryptCache;            // messageId -> 已解密正文（避免重复消费预密钥）
    bool m_decryptCacheLoaded = false;                // 本次登录是否已从磁盘加载解密缓存

    // M6.5: 本地加密持久化缓存（会话/消息/outbox/解密缓存/同步游标）
    LocalStore m_localStore;
};
