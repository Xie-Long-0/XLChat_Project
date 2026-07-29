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

    // ── 消息管理 ─────────────────────────────────────────────────────────────
    qint64 sendMessage(qint64 conversationId, qint64 senderId,
                       const QString &content, const QString &contentType = "text");
    std::optional<MessageInfo> getMessage(qint64 messageId);
    QList<MessageInfo> getMessages(qint64 conversationId, qint64 beforeId = 0, int limit = 50);
    QList<MessageInfo> syncMessages(qint64 conversationId, qint64 afterId, int limit = 100);
    bool updateMessageStatus(qint64 messageId, const QString &status);
    bool updateMessagesReadStatus(qint64 conversationId, qint64 readerId);
    int getUnreadCount(qint64 conversationId, qint64 userId);

private:
    bool openDatabase();
    void closeDatabase();
    bool runMigrations();
    bool migrateToV1();
    bool migrateToV2();
    bool migrateToV3();

    QString m_connectionName;
};
