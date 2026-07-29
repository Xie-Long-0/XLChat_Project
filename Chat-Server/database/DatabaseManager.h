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

    // ── 设备管理 ─────────────────────────────────────────────────────────────
    bool registerDevice(qint64 userId,
                        const QString &deviceId,
                        const QString &deviceName,
                        const QString &platform);
    QList<QJsonObject> getDevicesByUserId(qint64 userId);
    bool removeDevice(qint64 userId, const QString &deviceId);

private:
    bool openDatabase();
    void closeDatabase();
    bool runMigrations();
    bool migrateToV1();
    bool migrateToV2();

    QString m_connectionName;
};
