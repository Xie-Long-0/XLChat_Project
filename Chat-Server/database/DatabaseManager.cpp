#include "DatabaseManager.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QDateTime>

// ── 构造 / 析构 ──────────────────────────────────────────────────────────────
DatabaseManager::DatabaseManager(const QString &connectionName)
    : m_connectionName(connectionName)
{
}

DatabaseManager::~DatabaseManager()
{
    closeDatabase();
}

// ── 初始化 ───────────────────────────────────────────────────────────────────
bool DatabaseManager::initialize()
{
    if (!openDatabase()) {
        return false;
    }
    return runMigrations();
}

bool DatabaseManager::openDatabase()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        return true;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName("resources/db/chatapp.db");

    if (!db.open()) {
        qCritical() << "[DB] Failed to open:" << db.lastError().text();
        return false;
    }

    // 启用 WAL 模式和外键约束
    QSqlQuery q(db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA foreign_keys=ON");
    return true;
}

void DatabaseManager::closeDatabase()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

// ── 迁移 ─────────────────────────────────────────────────────────────────────
bool DatabaseManager::runMigrations()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 创建 schema_version 表（如果不存在）
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "  version INTEGER PRIMARY KEY,"
            "  applied_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")")) {
        qCritical() << "[DB] Failed to create schema_version:" << q.lastError().text();
        return false;
    }

    // 获取当前版本
    int currentVersion = 0;
    if (q.exec("SELECT MAX(version) FROM schema_version") && q.next()) {
        currentVersion = q.value(0).toInt();
    }

    qDebug() << "[DB] Current schema version:" << currentVersion;

    // 按版本顺序执行迁移
    if (currentVersion < 1) {
        if (!migrateToV1()) return false;
    }
    if (currentVersion < 2) {
        if (!migrateToV2()) return false;
    }

    return true;
}

// V1：基础用户表（兼容旧结构，但升级为 PBKDF2 存储格式）
bool DatabaseManager::migrateToV1()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    qDebug() << "[DB] Migrating to V1...";

    // 如果旧 users 表存在，先备份再重建
    bool oldTableExists = false;
    if (q.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='users'") && q.next()) {
        oldTableExists = true;
    }

    if (oldTableExists) {
        // 检查旧表结构（只有 id, username, password 三列）
        if (q.exec("PRAGMA table_info(users)")) {
            int colCount = 0;
            while (q.next()) { ++colCount; }
            if (colCount == 3) {
                // 旧结构：重命名为 old_users
                if (!q.exec("ALTER TABLE users RENAME TO old_users")) {
                    qCritical() << "[DB] V1: Failed to rename old users table:" << q.lastError().text();
                    return false;
                }
            }
        }
    }

    // 创建新 users 表
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS users ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  username TEXT UNIQUE NOT NULL,"
            "  email TEXT,"
            "  phone TEXT,"
            "  password_hash TEXT NOT NULL,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")")) {
        qCritical() << "[DB] V1: Failed to create users table:" << q.lastError().text();
        return false;
    }

    // 迁移旧数据（旧密码格式为纯 SHA-256，标记为 v0 以便后续识别）
    bool oldUsersExist = false;
    if (q.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='old_users'") && q.next()) {
        oldUsersExist = true;
    }
    if (oldUsersExist) {
        q.exec(
            "INSERT OR IGNORE INTO users (username, password_hash) "
            "SELECT username, 'v0:0::' || password FROM old_users");
        q.exec("DROP TABLE old_users");
    }

    // 记录版本
    q.prepare("INSERT INTO schema_version (version) VALUES (1)");
    if (!q.exec()) {
        qCritical() << "[DB] V1: Failed to record version:" << q.lastError().text();
        return false;
    }

    qDebug() << "[DB] Migration V1 complete";
    return true;
}

// V2：新增 devices、sessions、login_audit 表
bool DatabaseManager::migrateToV2()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    qDebug() << "[DB] Migrating to V2...";

    // devices 表
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS devices ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id INTEGER NOT NULL,"
            "  device_id TEXT NOT NULL,"
            "  device_name TEXT,"
            "  platform TEXT,"
            "  public_key TEXT,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  last_seen_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
            "  UNIQUE(user_id, device_id)"
            ")")) {
        qCritical() << "[DB] V2: Failed to create devices table:" << q.lastError().text();
        return false;
    }

    // sessions 表
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS sessions ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id INTEGER NOT NULL,"
            "  device_id TEXT,"
            "  token_hash TEXT NOT NULL UNIQUE,"
            "  login_ip TEXT,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  last_active_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  expires_at TEXT NOT NULL,"
            "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
            ")")) {
        qCritical() << "[DB] V2: Failed to create sessions table:" << q.lastError().text();
        return false;
    }

    // login_audit 表
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS login_audit ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id INTEGER,"
            "  ip_address TEXT,"
            "  success INTEGER NOT NULL,"
            "  failure_reason TEXT,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")")) {
        qCritical() << "[DB] V2: Failed to create login_audit table:" << q.lastError().text();
        return false;
    }

    // 记录版本
    q.prepare("INSERT INTO schema_version (version) VALUES (2)");
    if (!q.exec()) {
        qCritical() << "[DB] V2: Failed to record version:" << q.lastError().text();
        return false;
    }

    qDebug() << "[DB] Migration V2 complete";
    return true;
}

// ── 用户管理 ─────────────────────────────────────────────────────────────────
bool DatabaseManager::userExists(const QString &username)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM users WHERE username = ?");
    q.addBindValue(username);
    if (q.exec() && q.next()) {
        return q.value(0).toInt() > 0;
    }
    return false;
}

std::optional<UserInfo> DatabaseManager::getUserByUsername(const QString &username)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT id, username, email, phone, password_hash, created_at, updated_at "
        "FROM users WHERE username = ?");
    q.addBindValue(username);
    if (q.exec() && q.next()) {
        UserInfo u;
        u.id = q.value(0).toLongLong();
        u.username = q.value(1).toString();
        u.email = q.value(2).toString();
        u.phone = q.value(3).toString();
        u.passwordHash = q.value(4).toString();
        u.createdAt = q.value(5).toString();
        u.updatedAt = q.value(6).toString();
        return u;
    }
    return std::nullopt;
}

qint64 DatabaseManager::registerUser(const QString &username,
                                     const QString &email,
                                     const QString &phone,
                                     const QString &passwordHash)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO users (username, email, phone, password_hash) VALUES (?, ?, ?, ?)");
    q.addBindValue(username);
    q.addBindValue(email);
    q.addBindValue(phone);
    q.addBindValue(passwordHash);
    if (q.exec()) {
        return q.lastInsertId().toLongLong();
    }
    qWarning() << "[DB] registerUser failed:" << q.lastError().text();
    return -1;
}

// ── Session 管理 ─────────────────────────────────────────────────────────────
qint64 DatabaseManager::createSession(qint64 userId,
                                      const QString &deviceId,
                                      const QString &tokenHash,
                                      const QString &loginIp,
                                      int ttlSeconds)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    const QString expiresAt = QDateTime::currentDateTimeUtc()
                                  .addSecs(ttlSeconds)
                                  .toString(Qt::ISODate);
    q.prepare(
        "INSERT INTO sessions (user_id, device_id, token_hash, login_ip, expires_at) "
        "VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    q.addBindValue(tokenHash);
    q.addBindValue(loginIp);
    q.addBindValue(expiresAt);
    if (q.exec()) {
        return q.lastInsertId().toLongLong();
    }
    qWarning() << "[DB] createSession failed:" << q.lastError().text();
    return -1;
}

std::optional<SessionInfo> DatabaseManager::getSessionByTokenHash(const QString &tokenHash)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT id, user_id, device_id, login_ip, created_at, last_active_at, expires_at "
        "FROM sessions WHERE token_hash = ?");
    q.addBindValue(tokenHash);
    if (q.exec() && q.next()) {
        SessionInfo s;
        s.id = q.value(0).toLongLong();
        s.userId = q.value(1).toLongLong();
        s.deviceId = q.value(2).toString();
        s.loginIp = q.value(3).toString();
        s.createdAt = q.value(4).toString();
        s.lastActiveAt = q.value(5).toString();
        s.expiresAt = q.value(6).toString();
        return s;
    }
    return std::nullopt;
}

bool DatabaseManager::updateSessionLastActive(qint64 sessionId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "UPDATE sessions SET last_active_at = datetime('now') WHERE id = ?");
    q.addBindValue(sessionId);
    return q.exec();
}

bool DatabaseManager::deleteSession(qint64 sessionId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM sessions WHERE id = ?");
    q.addBindValue(sessionId);
    return q.exec();
}

bool DatabaseManager::deleteSessionsByUserId(qint64 userId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM sessions WHERE user_id = ?");
    q.addBindValue(userId);
    return q.exec();
}

QList<SessionInfo> DatabaseManager::getSessionsByUserId(qint64 userId)
{
    QList<SessionInfo> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT id, device_id, login_ip, created_at, last_active_at, expires_at "
        "FROM sessions WHERE user_id = ? ORDER BY created_at DESC");
    q.addBindValue(userId);
    if (q.exec()) {
        while (q.next()) {
            SessionInfo s;
            s.id = q.value(0).toLongLong();
            s.userId = userId;
            s.deviceId = q.value(1).toString();
            s.loginIp = q.value(2).toString();
            s.createdAt = q.value(3).toString();
            s.lastActiveAt = q.value(4).toString();
            s.expiresAt = q.value(5).toString();
            result.append(s);
        }
    }
    return result;
}

// ── 登录审计 ─────────────────────────────────────────────────────────────────
void DatabaseManager::recordLoginAttempt(qint64 userId,
                                         const QString &ipAddress,
                                         bool success,
                                         const QString &failureReason)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO login_audit (user_id, ip_address, success, failure_reason) "
        "VALUES (?, ?, ?, ?)");
    q.addBindValue(userId);
    q.addBindValue(ipAddress);
    q.addBindValue(success ? 1 : 0);
    q.addBindValue(failureReason);
    q.exec();
}

int DatabaseManager::recentFailedLoginCount(const QString &ipAddress, int windowSeconds)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT COUNT(*) FROM login_audit "
        "WHERE ip_address = ? AND success = 0 "
        "AND created_at >= datetime('now', ?)");
    q.addBindValue(ipAddress);
    q.addBindValue(QString("-%1 seconds").arg(windowSeconds));
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

int DatabaseManager::recentFailedLoginCountForUser(qint64 userId, int windowSeconds)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT COUNT(*) FROM login_audit "
        "WHERE user_id = ? AND success = 0 "
        "AND created_at >= datetime('now', ?)");
    q.addBindValue(userId);
    q.addBindValue(QString("-%1 seconds").arg(windowSeconds));
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

// ── 设备管理 ─────────────────────────────────────────────────────────────────
bool DatabaseManager::registerDevice(qint64 userId,
                                     const QString &deviceId,
                                     const QString &deviceName,
                                     const QString &platform)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO devices (user_id, device_id, device_name, platform) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(user_id, device_id) DO UPDATE SET "
        "  last_seen_at = datetime('now'),"
        "  device_name = excluded.device_name,"
        "  platform = excluded.platform");
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    q.addBindValue(deviceName);
    q.addBindValue(platform);
    if (!q.exec()) {
        qWarning() << "[DB] registerDevice failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QList<QJsonObject> DatabaseManager::getDevicesByUserId(qint64 userId)
{
    QList<QJsonObject> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT device_id, device_name, platform, created_at, last_seen_at "
        "FROM devices WHERE user_id = ? ORDER BY last_seen_at DESC");
    q.addBindValue(userId);
    if (q.exec()) {
        while (q.next()) {
            QJsonObject obj;
            obj["deviceId"] = q.value(0).toString();
            obj["deviceName"] = q.value(1).toString();
            obj["platform"] = q.value(2).toString();
            obj["createdAt"] = q.value(3).toString();
            obj["lastSeenAt"] = q.value(4).toString();
            result.append(obj);
        }
    }
    return result;
}

bool DatabaseManager::removeDevice(qint64 userId, const QString &deviceId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM devices WHERE user_id = ? AND device_id = ?");
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    return q.exec();
}
