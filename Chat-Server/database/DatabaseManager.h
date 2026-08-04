#pragma once

#include <QString>
#include <QJsonObject>
#include <QSqlDatabase>

#include <optional>

// ── 数据结构 ─────────────────────────────────────────────────────────────────
struct UserInfo
{
    qint64 id = 0;
    QString username;
    QString email;
    QString phone;
    QString passwordHash;
    QString createdAt;
    QString updatedAt;
};

struct SessionInfo
{
    qint64 id = 0;
    qint64 userId = 0;
    QString deviceId;
    QString tokenHash;
    QString loginIp;
    QString createdAt;
    QString lastActiveAt;
    QString expiresAt;
};

struct ContactInfo
{
    qint64 id = 0;
    qint64 userId = 0;
    qint64 contactUserId = 0;
    QString contactUsername;
    QString createdAt;
};

struct ConversationInfo
{
    qint64 id = 0;
    QString type; // "private"
    QString createdAt;
    QString updatedAt;
    // 对于一对一会话，记录对方信息
    qint64 peerUserId = 0;
    QString peerUsername;
    QString lastMessage;
    qint64 lastMessageId = 0;
    QString lastMessageAt;
    int unreadCount = 0;
};

struct MessageInfo
{
    qint64 id = 0;
    qint64 conversationId = 0;
    qint64 senderId = 0;
    QString senderUsername;
    QString content;
    QString contentType; // "text"
    QString status;      // "sending", "sent", "delivered", "read", "failed"
    QString createdAt;
    QString clientMessageId; // M5.5: 客户端幂等键
};

// M5.5: 账号级同步事件
struct SyncEventInfo
{
    qint64 seq = 0;
    qint64 userId = 0;
    QString eventType; // "message", "contact_added", "receipt"
    QString payload;   // JSON
    QString createdAt;
};

// ── DatabaseManager ──────────────────────────────────────────────────────────
class DatabaseManager
{
public:
    explicit DatabaseManager(const QString &connectionName = QStringLiteral("main"));
    ~DatabaseManager();

    // 初始化：打开连接并运行迁移
    bool initialize();

    // ── 用户管理 ─────────────────────────────────────────────────────────────
    bool userExists(const QString &username);
    std::optional<UserInfo> getUserByUsername(const QString &username);
    qint64 registerUser(const QString &username,
                        const QString &email,
                        const QString &phone,
                        const QString &passwordHash);

    // ── Session 管理 ─────────────────────────────────────────────────────────
    qint64 createSession(qint64 userId,
                         const QString &deviceId,
                         const QString &tokenHash,
                         const QString &loginIp,
                         int ttlSeconds = 86400 * 7); // 7 天
    std::optional<SessionInfo> getSessionByTokenHash(const QString &tokenHash);
    std::optional<SessionInfo> getSessionById(qint64 sessionId); // M5.5: 续期时校验 token
    bool updateSessionLastActive(qint64 sessionId);
    bool deleteSession(qint64 sessionId);
    bool deleteSessionsByUserId(qint64 userId);
    QList<SessionInfo> getSessionsByUserId(qint64 userId);

    // ── 登录审计 ─────────────────────────────────────────────────────────────
    void recordLoginAttempt(qint64 userId,
                            const QString &ipAddress,
                            bool success,
                            const QString &failureReason = {});
    int recentFailedLoginCount(const QString &ipAddress, int windowSeconds = 300);
    int recentFailedLoginCountForUser(qint64 userId, int windowSeconds = 300);

    // ── 设备管理 ─────────────────────────────────────────────────────────────────
    bool registerDevice(qint64 userId,
                        const QString &deviceId,
                        const QString &deviceName,
                        const QString &platform);
    QList<QJsonObject> getDevicesByUserId(qint64 userId);
    bool removeDevice(qint64 userId, const QString &deviceId);

    // ── 用户搜索 ─────────────────────────────────────────────────────────────────
    QList<UserInfo> searchUsers(const QString &query, int limit = 20);

    // ── 联系人管理 ─────────────────────────────────────────────────────────────
    bool addContact(qint64 userId, qint64 contactUserId);
    bool removeContact(qint64 userId, qint64 contactUserId);
    QList<ContactInfo> getContacts(qint64 userId);
    bool isContact(qint64 userId, qint64 contactUserId);

    // ── 会话管理 ─────────────────────────────────────────────────────────────
    qint64 getOrCreatePrivateConversation(qint64 userId1, qint64 userId2);
    QList<ConversationInfo> getConversationsForUser(qint64 userId);
    std::optional<ConversationInfo> getConversation(qint64 conversationId);
    // M5.5: 授权检查（先授权再查询）
    bool isConversationMember(qint64 conversationId, qint64 userId);
    bool canAccessMessage(qint64 messageId, qint64 userId);

    // ── 消息管理 ─────────────────────────────────────────────────────────────
    qint64 sendMessage(qint64 conversationId, qint64 senderId,
                       const QString &content, const QString &contentType = "text",
                       const QString &clientMessageId = {},
                       const QString &senderDeviceId = {});
    std::optional<MessageInfo> getMessage(qint64 messageId);
    // M5.5: 客户端幂等键去重
    std::optional<MessageInfo> getMessageByClientKey(qint64 senderId,
                                                     const QString &senderDeviceId,
                                                     const QString &clientMessageId);
    QList<MessageInfo> getMessages(qint64 conversationId, qint64 beforeId = 0, int limit = 50);
    QList<MessageInfo> syncMessages(qint64 conversationId, qint64 afterId, int limit = 100);
    bool updateMessageStatus(qint64 messageId, const QString &status);
    bool updateMessagesReadStatus(qint64 conversationId, qint64 readerId);
    int getUnreadCount(qint64 conversationId, qint64 userId);

    // M5.5: 消息回执（per-recipient，替代全局状态聚合）
    bool recordMessageReceipt(qint64 messageId, qint64 userId,
                              const QString &deviceId, const QString &status);
    int receiptCount(qint64 messageId, const QString &status); // "delivered" / "read"
    bool updateMemberReadCursor(qint64 conversationId, qint64 userId, qint64 messageId);

    // M5.5: 同步事件流
    qint64 appendSyncEvent(qint64 userId, const QString &eventType, const QString &payloadJson);
    QList<SyncEventInfo> getSyncEvents(qint64 userId, qint64 afterSeq, int limit = 200);

private:
    bool openDatabase();
    void closeDatabase();
    bool runMigrations();
    bool migrateToV1();
    bool migrateToV2();
    bool migrateToV3();
    bool migrateToV4();

    QString m_connectionName;
};
