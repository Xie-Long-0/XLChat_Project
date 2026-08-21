#include "DatabaseManager.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>

static const QString DatabasePath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/XYChat-Server/data/db";

DatabaseManager::DatabaseManager(const QString &connectionName)
    : m_connectionName(connectionName)
{
}

DatabaseManager::~DatabaseManager()
{
    closeDatabase();
}

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

    if (auto dir = QDir(DatabasePath); !dir.exists())
    {
        dir.mkpath(".");
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(DatabasePath + "/chatapp.db");
    // M6: 多 handler 线程并发写时避免 SQLITE_BUSY 立即失败
    db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");

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

// 迁移
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
    if (currentVersion < 3) {
        if (!migrateToV3()) return false;
    }
    if (currentVersion < 4) {
        if (!migrateToV4()) return false;
    }
    if (currentVersion < 5) {
        if (!migrateToV5()) return false;
    }
    if (currentVersion < 6) {
        if (!migrateToV6()) return false;
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

// V3：新增联系人、会话、消息表
bool DatabaseManager::migrateToV3()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    qDebug() << "[DB] Migrating to V3...";

    // contacts 表（双向联系人关系）
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS contacts ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id INTEGER NOT NULL,"
            "  contact_user_id INTEGER NOT NULL,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
            "  FOREIGN KEY (contact_user_id) REFERENCES users(id) ON DELETE CASCADE,"
            "  UNIQUE(user_id, contact_user_id)"
            ")")) {
        qCritical() << "[DB] V3: Failed to create contacts table:" << q.lastError().text();
        return false;
    }

    // conversations 表
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS conversations ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  type TEXT NOT NULL DEFAULT 'private',"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")")) {
        qCritical() << "[DB] V3: Failed to create conversations table:" << q.lastError().text();
        return false;
    }

    // conversation_members 表
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS conversation_members ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  conversation_id INTEGER NOT NULL,"
            "  user_id INTEGER NOT NULL,"
            "  joined_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  last_read_message_id INTEGER DEFAULT 0,"
            "  FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE,"
            "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
            "  UNIQUE(conversation_id, user_id)"
            ")")) {
        qCritical() << "[DB] V3: Failed to create conversation_members table:" << q.lastError().text();
        return false;
    }

    // messages 表
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS messages ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  conversation_id INTEGER NOT NULL,"
            "  sender_id INTEGER NOT NULL,"
            "  content TEXT NOT NULL,"
            "  content_type TEXT NOT NULL DEFAULT 'text',"
            "  status TEXT NOT NULL DEFAULT 'sent',"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE,"
            "  FOREIGN KEY (sender_id) REFERENCES users(id) ON DELETE CASCADE"
            ")")) {
        qCritical() << "[DB] V3: Failed to create messages table:" << q.lastError().text();
        return false;
    }

    // 消息表索引
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_conversation ON messages(conversation_id, id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender_id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_conv_members_user ON conversation_members(user_id)");

    // 记录版本
    q.prepare("INSERT INTO schema_version (version) VALUES (3)");
    if (!q.exec()) {
        qCritical() << "[DB] V3: Failed to record version:" << q.lastError().text();
        return false;
    }

    qDebug() << "[DB] Migration V3 complete";
    return true;
}

// V4（M5.5）：消息幂等键、回执表、同步事件表
bool DatabaseManager::migrateToV4()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    qDebug() << "[DB] Migrating to V4...";

    // messages 表新增客户端幂等键与发送设备列
    if (!q.exec("ALTER TABLE messages ADD COLUMN client_message_id TEXT")) {
        qCritical() << "[DB] V4: Failed to add messages.client_message_id:" << q.lastError().text();
        return false;
    }
    if (!q.exec("ALTER TABLE messages ADD COLUMN sender_device_id TEXT")) {
        qCritical() << "[DB] V4: Failed to add messages.sender_device_id:" << q.lastError().text();
        return false;
    }
    // 幂等唯一约束：同一设备重复提交同一 client_message_id 时返回已有消息
    // （SQLite 唯一索引中 NULL 互不相等，存量旧数据不受影响）
    if (!q.exec(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_messages_client_key "
            "ON messages(sender_id, sender_device_id, client_message_id) "
            "WHERE client_message_id IS NOT NULL AND client_message_id != ''")) {
        qCritical() << "[DB] V4: Failed to create message idempotency index:" << q.lastError().text();
        return false;
    }

    // message_receipts 表：按接收者/设备维度的送达与已读回执
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS message_receipts ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  message_id INTEGER NOT NULL,"
            "  user_id INTEGER NOT NULL,"
            "  device_id TEXT,"
            "  delivered_at TEXT,"
            "  read_at TEXT,"
            "  FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE,"
            "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
            "  UNIQUE(message_id, user_id, device_id)"
            ")")) {
        qCritical() << "[DB] V4: Failed to create message_receipts table:" << q.lastError().text();
        return false;
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_receipts_message ON message_receipts(message_id)");

    // sync_events 表：账号级增量同步事件流（消息/联系人/回执等）
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS sync_events ("
            "  seq INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id INTEGER NOT NULL,"
            "  event_type TEXT NOT NULL,"
            "  payload TEXT NOT NULL DEFAULT '{}',"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
            ")")) {
        qCritical() << "[DB] V4: Failed to create sync_events table:" << q.lastError().text();
        return false;
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_sync_events_user ON sync_events(user_id, seq)");

    // 记录版本
    q.prepare("INSERT INTO schema_version (version) VALUES (4)");
    if (!q.exec()) {
        qCritical() << "[DB] V4: Failed to record version:" << q.lastError().text();
        return false;
    }

    qDebug() << "[DB] Migration V4 complete";
    return true;
}

// V5（M6）：端到端加密密钥表（设备身份公钥 + 一次性预密钥公钥）
bool DatabaseManager::migrateToV5()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    qDebug() << "[DB] Migrating to V5...";

    // device_identity_keys 表：每设备一个 X25519 身份公钥（仅存公钥）
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS device_identity_keys ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id INTEGER NOT NULL,"
            "  device_id TEXT NOT NULL,"
            "  identity_pub TEXT NOT NULL,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
            "  UNIQUE(user_id, device_id)"
            ")")) {
        qCritical() << "[DB] V5: Failed to create device_identity_keys table:" << q.lastError().text();
        return false;
    }

    // prekeys 表：一次性预密钥公钥，状态 unused -> claimed -> used
    // claimed_at 用于超时回收：认领后未被消费（发送方放弃/掉线）的
    // 预密钥在 claimPrekeys 中回退为 unused，避免泄漏导致预密钥池枯竭
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS prekeys ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id INTEGER NOT NULL,"
            "  device_id TEXT NOT NULL,"
            "  pub TEXT NOT NULL,"
            "  status TEXT NOT NULL DEFAULT 'unused',"
            "  claimed_at TEXT,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
            ")")) {
        qCritical() << "[DB] V5: Failed to create prekeys table:" << q.lastError().text();
        return false;
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_prekeys_lookup "
           "ON prekeys(user_id, device_id, status)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_prekeys_claimed_at ON prekeys(claimed_at)");

    // 记录版本
    q.prepare("INSERT INTO schema_version (version) VALUES (5)");
    if (!q.exec()) {
        qCritical() << "[DB] V5: Failed to record version:" << q.lastError().text();
        return false;
    }

    qDebug() << "[DB] Migration V5 complete";
    return true;
}

// V6（M6 修复）：兼容中间版本构建创建的 prekeys 表，补齐 claimed_at 列
bool DatabaseManager::migrateToV6()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    qDebug() << "[DB] Migrating to V6...";

    bool hasClaimedAt = false;
    if (q.exec("PRAGMA table_info(prekeys)")) {
        while (q.next()) {
            if (q.value(1).toString() == "claimed_at") {
                hasClaimedAt = true;
                break;
            }
        }
    }
    if (!hasClaimedAt && !q.exec("ALTER TABLE prekeys ADD COLUMN claimed_at TEXT")) {
        qCritical() << "[DB] V6: Failed to add prekeys.claimed_at:" << q.lastError().text();
        return false;
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_prekeys_claimed_at ON prekeys(claimed_at)");

    q.prepare("INSERT INTO schema_version (version) VALUES (6)");
    if (!q.exec()) {
        qCritical() << "[DB] V6: Failed to record version:" << q.lastError().text();
        return false;
    }

    qDebug() << "[DB] Migration V6 complete";
    return true;
}

// 用户管理
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

// Session 管理
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

// M5.5: 按 ID 查询 session（含 token_hash，用于续期时校验客户端携带的 token）
std::optional<SessionInfo> DatabaseManager::getSessionById(qint64 sessionId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT id, user_id, device_id, token_hash, login_ip, created_at, last_active_at, expires_at "
        "FROM sessions WHERE id = ?");
    q.addBindValue(sessionId);
    if (q.exec() && q.next()) {
        SessionInfo s;
        s.id = q.value(0).toLongLong();
        s.userId = q.value(1).toLongLong();
        s.deviceId = q.value(2).toString();
        s.tokenHash = q.value(3).toString();
        s.loginIp = q.value(4).toString();
        s.createdAt = q.value(5).toString();
        s.lastActiveAt = q.value(6).toString();
        s.expiresAt = q.value(7).toString();
        return s;
    }
    return std::nullopt;
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

// 登录审计
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

// 设备管理
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
    if (!q.exec()) {
        return false;
    }
    // M6: 同步清除该设备密钥材料，删除后不能再收到新消息
    return removeDeviceKeys(userId, deviceId);
}

// 用户搜索
QList<UserInfo> DatabaseManager::searchUsers(const QString &query, int limit)
{
    QList<UserInfo> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT id, username, email, phone FROM users "
        "WHERE username LIKE ? LIMIT ?");
    q.addBindValue(QString("%1%").arg(query));
    q.addBindValue(limit);
    if (q.exec()) {
        while (q.next()) {
            UserInfo u;
            u.id = q.value(0).toLongLong();
            u.username = q.value(1).toString();
            u.email = q.value(2).toString();
            u.phone = q.value(3).toString();
            result.append(u);
        }
    }
    return result;
}

// 联系人管理
bool DatabaseManager::addContact(qint64 userId, qint64 contactUserId)
{
    if (userId == contactUserId) return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 双向添加联系人
    q.prepare("INSERT OR IGNORE INTO contacts (user_id, contact_user_id) VALUES (?, ?)");
    q.addBindValue(userId);
    q.addBindValue(contactUserId);
    if (!q.exec()) return false;

    q.prepare("INSERT OR IGNORE INTO contacts (user_id, contact_user_id) VALUES (?, ?)");
    q.addBindValue(contactUserId);
    q.addBindValue(userId);
    return q.exec();
}

bool DatabaseManager::removeContact(qint64 userId, qint64 contactUserId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM contacts WHERE (user_id = ? AND contact_user_id = ?) "
              "OR (user_id = ? AND contact_user_id = ?)");
    q.addBindValue(userId);
    q.addBindValue(contactUserId);
    q.addBindValue(contactUserId);
    q.addBindValue(userId);
    return q.exec();
}

QList<ContactInfo> DatabaseManager::getContacts(qint64 userId)
{
    QList<ContactInfo> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT c.id, c.user_id, c.contact_user_id, u.username, c.created_at "
        "FROM contacts c JOIN users u ON c.contact_user_id = u.id "
        "WHERE c.user_id = ? ORDER BY c.created_at DESC");
    q.addBindValue(userId);
    if (q.exec()) {
        while (q.next()) {
            ContactInfo ci;
            ci.id = q.value(0).toLongLong();
            ci.userId = q.value(1).toLongLong();
            ci.contactUserId = q.value(2).toLongLong();
            ci.contactUsername = q.value(3).toString();
            ci.createdAt = q.value(4).toString();
            result.append(ci);
        }
    }
    return result;
}

bool DatabaseManager::isContact(qint64 userId, qint64 contactUserId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM contacts WHERE user_id = ? AND contact_user_id = ?");
    q.addBindValue(userId);
    q.addBindValue(contactUserId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt() > 0;
    }
    return false;
}

// 会话管理
qint64 DatabaseManager::getOrCreatePrivateConversation(qint64 userId1, qint64 userId2)
{
    if (userId1 == userId2) return -1;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 查找已存在的会话：两个用户都是成员的 private 会话
    q.prepare(
        "SELECT cm1.conversation_id FROM conversation_members cm1 "
        "JOIN conversation_members cm2 ON cm1.conversation_id = cm2.conversation_id "
        "JOIN conversations c ON cm1.conversation_id = c.id "
        "WHERE cm1.user_id = ? AND cm2.user_id = ? AND c.type = 'private'");
    q.addBindValue(userId1);
    q.addBindValue(userId2);
    if (q.exec() && q.next()) {
        return q.value(0).toLongLong();
    }

    // 创建新会话
    q.prepare("INSERT INTO conversations (type) VALUES ('private')");
    if (!q.exec()) return -1;
    const qint64 convId = q.lastInsertId().toLongLong();

    // 添加两个成员
    q.prepare("INSERT INTO conversation_members (conversation_id, user_id) VALUES (?, ?)");
    q.addBindValue(convId);
    q.addBindValue(userId1);
    q.exec();

    q.prepare("INSERT INTO conversation_members (conversation_id, user_id) VALUES (?, ?)");
    q.addBindValue(convId);
    q.addBindValue(userId2);
    q.exec();

    return convId;
}

QList<ConversationInfo> DatabaseManager::getConversationsForUser(qint64 userId)
{
    QList<ConversationInfo> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 获取用户参与的所有会话，包含最后一条消息和未读数
    q.prepare(
        "SELECT c.id, c.type, c.created_at, c.updated_at, "
        "  m.id, m.content, m.created_at, m.sender_id, "
        "  cm.last_read_message_id "
        "FROM conversation_members cm "
        "JOIN conversations c ON cm.conversation_id = c.id "
        "LEFT JOIN messages m ON m.id = ("
        "  SELECT MAX(id) FROM messages WHERE conversation_id = c.id"
        ") "
        "WHERE cm.user_id = ? "
        "ORDER BY COALESCE(m.created_at, c.updated_at) DESC");
    q.addBindValue(userId);
    if (q.exec()) {
        while (q.next()) {
            ConversationInfo ci;
            ci.id = q.value(0).toLongLong();
            ci.type = q.value(1).toString();
            ci.createdAt = q.value(2).toString();
            ci.updatedAt = q.value(3).toString();
            ci.lastMessageId = q.value(4).toLongLong();
            ci.lastMessage = q.value(5).toString();
            ci.lastMessageAt = q.value(6).toString();

            // 查询对方用户信息（一对一会话）
            qint64 senderId = q.value(7).toLongLong();
            Q_UNUSED(senderId);

            // 计算未读数
            const qint64 lastReadId = q.value(8).toLongLong();
            QSqlQuery countQ(db);
            countQ.prepare(
                "SELECT COUNT(*) FROM messages "
                "WHERE conversation_id = ? AND id > ? AND sender_id != ?");
            countQ.addBindValue(ci.id);
            countQ.addBindValue(lastReadId);
            countQ.addBindValue(userId);
            if (countQ.exec() && countQ.next()) {
                ci.unreadCount = countQ.value(0).toInt();
            }

            result.append(ci);
        }
    }

    // 填充 peer 信息
    for (auto &ci : result) {
        if (ci.type == "private") {
            QSqlQuery peerQ(db);
            peerQ.prepare(
                "SELECT cm2.user_id, u.username FROM conversation_members cm2 "
                "JOIN users u ON cm2.user_id = u.id "
                "WHERE cm2.conversation_id = ? AND cm2.user_id != ?");
            peerQ.addBindValue(ci.id);
            peerQ.addBindValue(userId);
            if (peerQ.exec() && peerQ.next()) {
                ci.peerUserId = peerQ.value(0).toLongLong();
                ci.peerUsername = peerQ.value(1).toString();
            }
        }
    }

    return result;
}

std::optional<ConversationInfo> DatabaseManager::getConversation(qint64 conversationId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT id, type, created_at, updated_at FROM conversations WHERE id = ?");
    q.addBindValue(conversationId);
    if (q.exec() && q.next()) {
        ConversationInfo ci;
        ci.id = q.value(0).toLongLong();
        ci.type = q.value(1).toString();
        ci.createdAt = q.value(2).toString();
        ci.updatedAt = q.value(3).toString();
        return ci;
    }
    return std::nullopt;
}

// M5.5: 会话成员授权检查
bool DatabaseManager::isConversationMember(qint64 conversationId, qint64 userId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT COUNT(*) FROM conversation_members "
        "WHERE conversation_id = ? AND user_id = ?");
    q.addBindValue(conversationId);
    q.addBindValue(userId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt() > 0;
    }
    return false;
}

// M5.5: 消息访问授权：请求者必须是该消息所属会话的成员
bool DatabaseManager::canAccessMessage(qint64 messageId, qint64 userId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT COUNT(*) FROM messages m "
        "JOIN conversation_members cm ON cm.conversation_id = m.conversation_id "
        "WHERE m.id = ? AND cm.user_id = ?");
    q.addBindValue(messageId);
    q.addBindValue(userId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt() > 0;
    }
    return false;
}

// 消息管理
qint64 DatabaseManager::sendMessage(qint64 conversationId, qint64 senderId,
                                    const QString &content, const QString &contentType,
                                    const QString &clientMessageId,
                                    const QString &senderDeviceId)
{
    // M5.5: 幂等去重：同一设备重复提交同一 client_message_id 时返回已有消息
    if (!clientMessageId.isEmpty()) {
        auto existing = getMessageByClientKey(senderId, senderDeviceId, clientMessageId);
        if (existing.has_value()) {
            return existing->id;
        }
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO messages "
        "  (conversation_id, sender_id, content, content_type, status, client_message_id, sender_device_id) "
        "VALUES (?, ?, ?, ?, 'sent', ?, ?)");
    q.addBindValue(conversationId);
    q.addBindValue(senderId);
    q.addBindValue(content);
    q.addBindValue(contentType);
    q.addBindValue(clientMessageId.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                             : QVariant(clientMessageId));
    q.addBindValue(senderDeviceId.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                            : QVariant(senderDeviceId));
    if (!q.exec()) {
        // 并发重试可能命中唯一索引：再查一次幂等键
        if (!clientMessageId.isEmpty()) {
            auto existing = getMessageByClientKey(senderId, senderDeviceId, clientMessageId);
            if (existing.has_value()) {
                return existing->id;
            }
        }
        qWarning() << "[DB] sendMessage failed:" << q.lastError().text();
        return -1;
    }

    const qint64 msgId = q.lastInsertId().toLongLong();

    // 更新会话的 updated_at
    q.prepare("UPDATE conversations SET updated_at = datetime('now') WHERE id = ?");
    q.addBindValue(conversationId);
    q.exec();

    return msgId;
}

// M5.5: 按客户端幂等键查找已存储消息
std::optional<MessageInfo> DatabaseManager::getMessageByClientKey(qint64 senderId,
                                                                  const QString &senderDeviceId,
                                                                  const QString &clientMessageId)
{
    if (clientMessageId.isEmpty()) {
        return std::nullopt;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT id, conversation_id, content, content_type, status, created_at "
        "FROM messages "
        "WHERE sender_id = ? AND sender_device_id = ? AND client_message_id = ?");
    q.addBindValue(senderId);
    q.addBindValue(senderDeviceId);
    q.addBindValue(clientMessageId);
    if (q.exec() && q.next()) {
        MessageInfo mi;
        mi.id = q.value(0).toLongLong();
        mi.conversationId = q.value(1).toLongLong();
        mi.senderId = senderId;
        mi.content = q.value(2).toString();
        mi.contentType = q.value(3).toString();
        mi.status = q.value(4).toString();
        mi.createdAt = q.value(5).toString();
        mi.clientMessageId = clientMessageId;
        return mi;
    }
    return std::nullopt;
}

std::optional<MessageInfo> DatabaseManager::getMessage(qint64 messageId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT m.id, m.conversation_id, m.sender_id, u.username, "
        "  m.content, m.content_type, m.status, m.created_at "
        "FROM messages m JOIN users u ON m.sender_id = u.id "
        "WHERE m.id = ?");
    q.addBindValue(messageId);
    if (q.exec() && q.next()) {
        MessageInfo mi;
        mi.id = q.value(0).toLongLong();
        mi.conversationId = q.value(1).toLongLong();
        mi.senderId = q.value(2).toLongLong();
        mi.senderUsername = q.value(3).toString();
        mi.content = q.value(4).toString();
        mi.contentType = q.value(5).toString();
        mi.status = q.value(6).toString();
        mi.createdAt = q.value(7).toString();
        return mi;
    }
    return std::nullopt;
}

QList<MessageInfo> DatabaseManager::getMessages(qint64 conversationId, qint64 beforeId, int limit)
{
    QList<MessageInfo> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (beforeId > 0) {
        q.prepare(
            "SELECT m.id, m.conversation_id, m.sender_id, u.username, "
            "  m.content, m.content_type, m.status, m.created_at "
            "FROM messages m JOIN users u ON m.sender_id = u.id "
            "WHERE m.conversation_id = ? AND m.id < ? "
            "ORDER BY m.id DESC LIMIT ?");
        q.addBindValue(conversationId);
        q.addBindValue(beforeId);
        q.addBindValue(limit);
    } else {
        q.prepare(
            "SELECT m.id, m.conversation_id, m.sender_id, u.username, "
            "  m.content, m.content_type, m.status, m.created_at "
            "FROM messages m JOIN users u ON m.sender_id = u.id "
            "WHERE m.conversation_id = ? "
            "ORDER BY m.id DESC LIMIT ?");
        q.addBindValue(conversationId);
        q.addBindValue(limit);
    }

    if (q.exec()) {
        while (q.next()) {
            MessageInfo mi;
            mi.id = q.value(0).toLongLong();
            mi.conversationId = q.value(1).toLongLong();
            mi.senderId = q.value(2).toLongLong();
            mi.senderUsername = q.value(3).toString();
            mi.content = q.value(4).toString();
            mi.contentType = q.value(5).toString();
            mi.status = q.value(6).toString();
            mi.createdAt = q.value(7).toString();
            result.prepend(mi); // 按时间正序排列
        }
    }
    return result;
}

QList<MessageInfo> DatabaseManager::syncMessages(qint64 conversationId, qint64 afterId, int limit)
{
    QList<MessageInfo> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT m.id, m.conversation_id, m.sender_id, u.username, "
        "  m.content, m.content_type, m.status, m.created_at "
        "FROM messages m JOIN users u ON m.sender_id = u.id "
        "WHERE m.conversation_id = ? AND m.id > ? "
        "ORDER BY m.id ASC LIMIT ?");
    q.addBindValue(conversationId);
    q.addBindValue(afterId);
    q.addBindValue(limit);
    if (q.exec()) {
        while (q.next()) {
            MessageInfo mi;
            mi.id = q.value(0).toLongLong();
            mi.conversationId = q.value(1).toLongLong();
            mi.senderId = q.value(2).toLongLong();
            mi.senderUsername = q.value(3).toString();
            mi.content = q.value(4).toString();
            mi.contentType = q.value(5).toString();
            mi.status = q.value(6).toString();
            mi.createdAt = q.value(7).toString();
            result.append(mi);
        }
    }
    return result;
}

bool DatabaseManager::updateMessageStatus(qint64 messageId, const QString &status)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("UPDATE messages SET status = ? WHERE id = ?");
    q.addBindValue(status);
    q.addBindValue(messageId);
    return q.exec();
}

bool DatabaseManager::updateMessagesReadStatus(qint64 conversationId, qint64 readerId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 将该会话中非该用户发送的消息标记为已读
    q.prepare(
        "UPDATE messages SET status = 'read' "
        "WHERE conversation_id = ? AND sender_id != ? AND status != 'read'");
    q.addBindValue(conversationId);
    q.addBindValue(readerId);
    if (!q.exec()) return false;

    // 更新 conversation_members 的 last_read_message_id
    QSqlQuery maxQ(db);
    maxQ.prepare("SELECT MAX(id) FROM messages WHERE conversation_id = ?");
    maxQ.addBindValue(conversationId);
    if (maxQ.exec() && maxQ.next()) {
        const qint64 maxId = maxQ.value(0).toLongLong();
        QSqlQuery updateQ(db);
        updateQ.prepare(
            "UPDATE conversation_members SET last_read_message_id = ? "
            "WHERE conversation_id = ? AND user_id = ?");
        updateQ.addBindValue(maxId);
        updateQ.addBindValue(conversationId);
        updateQ.addBindValue(readerId);
        updateQ.exec();
    }

    return true;
}

int DatabaseManager::getUnreadCount(qint64 conversationId, qint64 userId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT COUNT(*) FROM messages "
        "WHERE conversation_id = ? AND sender_id != ? AND status != 'read'");
    q.addBindValue(conversationId);
    q.addBindValue(userId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

// M5.5: 消息回执
bool DatabaseManager::recordMessageReceipt(qint64 messageId, qint64 userId,
                                           const QString &deviceId, const QString &status)
{
    if (status != "delivered" && status != "read") {
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (status == "delivered") {
        // 仅在尚无送达记录时写入，已读记录不回退
        q.prepare(
            "INSERT INTO message_receipts (message_id, user_id, device_id, delivered_at) "
            "VALUES (?, ?, ?, datetime('now')) "
            "ON CONFLICT(message_id, user_id, device_id) DO UPDATE SET "
            "  delivered_at = COALESCE(message_receipts.delivered_at, datetime('now'))");
    } else {
        q.prepare(
            "INSERT INTO message_receipts (message_id, user_id, device_id, delivered_at, read_at) "
            "VALUES (?, ?, ?, datetime('now'), datetime('now')) "
            "ON CONFLICT(message_id, user_id, device_id) DO UPDATE SET "
            "  delivered_at = COALESCE(message_receipts.delivered_at, datetime('now')),"
            "  read_at = COALESCE(message_receipts.read_at, datetime('now'))");
    }
    q.addBindValue(messageId);
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    if (!q.exec()) {
        qWarning() << "[DB] recordMessageReceipt failed:" << q.lastError().text();
        return false;
    }
    return true;
}

int DatabaseManager::receiptCount(qint64 messageId, const QString &status)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (status == "read") {
        q.prepare("SELECT COUNT(*) FROM message_receipts WHERE message_id = ? AND read_at IS NOT NULL");
    } else {
        q.prepare(
            "SELECT COUNT(*) FROM message_receipts "
            "WHERE message_id = ? AND (delivered_at IS NOT NULL OR read_at IS NOT NULL)");
    }
    q.addBindValue(messageId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

bool DatabaseManager::updateMemberReadCursor(qint64 conversationId, qint64 userId, qint64 messageId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    // 读游标只允许前进
    q.prepare(
        "UPDATE conversation_members SET last_read_message_id = ? "
        "WHERE conversation_id = ? AND user_id = ? AND last_read_message_id < ?");
    q.addBindValue(messageId);
    q.addBindValue(conversationId);
    q.addBindValue(userId);
    q.addBindValue(messageId);
    return q.exec();
}

// M5.5: 同步事件流
qint64 DatabaseManager::appendSyncEvent(qint64 userId, const QString &eventType,
                                        const QString &payloadJson)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO sync_events (user_id, event_type, payload) VALUES (?, ?, ?)");
    q.addBindValue(userId);
    q.addBindValue(eventType);
    q.addBindValue(payloadJson.isEmpty() ? "{}" : payloadJson);
    if (!q.exec()) {
        qWarning() << "[DB] appendSyncEvent failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

QList<SyncEventInfo> DatabaseManager::getSyncEvents(qint64 userId, qint64 afterSeq, int limit)
{
    QList<SyncEventInfo> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT seq, user_id, event_type, payload, created_at "
        "FROM sync_events WHERE user_id = ? AND seq > ? "
        "ORDER BY seq ASC LIMIT ?");
    q.addBindValue(userId);
    q.addBindValue(afterSeq);
    q.addBindValue(limit);
    if (q.exec()) {
        while (q.next()) {
            SyncEventInfo ev;
            ev.seq = q.value(0).toLongLong();
            ev.userId = q.value(1).toLongLong();
            ev.eventType = q.value(2).toString();
            ev.payload = q.value(3).toString();
            ev.createdAt = q.value(4).toString();
            result.append(ev);
        }
    }
    return result;
}

// M6: 端到端加密密钥管理
bool DatabaseManager::upsertIdentityKey(qint64 userId, const QString &deviceId,
                                        const QString &identityPub)
{
    if (deviceId.isEmpty() || identityPub.isEmpty()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    // M6 审查修复：身份公钥变更（重装/密钥丢失后重新生成）时，
    // 旧身份世代下未消费的预密钥与新身份不匹配，必须废弃，
    // 否则发送方会认领到无法解密的旧预密钥导致消息静默丢失
    {
        QSqlQuery checkQ(db);
        checkQ.prepare(
            "SELECT identity_pub FROM device_identity_keys "
            "WHERE user_id = ? AND device_id = ?");
        checkQ.addBindValue(userId);
        checkQ.addBindValue(deviceId);
        if (checkQ.exec() && checkQ.next()) {
            const QString oldPub = checkQ.value(0).toString();
            if (oldPub != identityPub) {
                QSqlQuery purgeQ(db);
                purgeQ.prepare(
                    "DELETE FROM prekeys WHERE user_id = ? AND device_id = ? "
                    "AND status IN ('unused', 'claimed')");
                purgeQ.addBindValue(userId);
                purgeQ.addBindValue(deviceId);
                if (!purgeQ.exec()) {
                    qWarning() << "[DB] upsertIdentityKey: failed to purge stale prekeys:"
                               << purgeQ.lastError().text();
                }
            }
        }
    }

    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO device_identity_keys (user_id, device_id, identity_pub) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(user_id, device_id) DO UPDATE SET "
        "  identity_pub = excluded.identity_pub,"
        "  updated_at = datetime('now')");
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    q.addBindValue(identityPub);
    if (!q.exec()) {
        qWarning() << "[DB] upsertIdentityKey failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QList<DeviceIdentityKey> DatabaseManager::getIdentityKeysByUser(qint64 userId)
{
    QList<DeviceIdentityKey> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT device_id, identity_pub FROM device_identity_keys WHERE user_id = ?");
    q.addBindValue(userId);
    if (q.exec()) {
        while (q.next()) {
            DeviceIdentityKey key;
            key.deviceId = q.value(0).toString();
            key.identityPub = q.value(1).toString();
            result.append(key);
        }
    }
    return result;
}

int DatabaseManager::uploadPrekeys(qint64 userId, const QString &deviceId,
                                   const QStringList &prekeyPubs)
{
    if (deviceId.isEmpty() || prekeyPubs.isEmpty()) {
        return -1;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        qWarning() << "[DB] uploadPrekeys: failed to begin transaction";
        return -1;
    }

    QSqlQuery q(db);
    q.prepare("INSERT INTO prekeys (user_id, device_id, pub) VALUES (?, ?, ?)");
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    int inserted = 0;
    bool ok = true;
    for (const QString &pub : prekeyPubs) {
        if (pub.isEmpty()) {
            continue;
        }
        q.bindValue(2, pub);
        if (!q.exec()) {
            ok = false;
            break;
        }
        ++inserted;
    }

    if (!ok || !db.commit()) {
        db.rollback();
        qWarning() << "[DB] uploadPrekeys failed:" << q.lastError().text();
        return -1;
    }
    return inserted;
}

int DatabaseManager::prekeyCount(qint64 userId, const QString &deviceId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT COUNT(*) FROM prekeys "
        "WHERE user_id = ? AND device_id = ? AND status = 'unused'");
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

// 事务内为每个有库存的设备认领一个 unused 预密钥（select + 条件 update，
// SQLite 写锁保证并发下同一预密钥只被认领一次）
QList<ClaimedPrekey> DatabaseManager::claimPrekeys(qint64 userId)
{
    QList<ClaimedPrekey> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    // BEGIN IMMEDIATE：立即获取写锁，避免事务中途升级锁失败
    {
        QSqlQuery beginQ(db);
        if (!beginQ.exec("BEGIN IMMEDIATE")) {
            qWarning() << "[DB] claimPrekeys: failed to begin immediate transaction:";
            return result;
        }
    }

    // 超时认领回收：认领后 10 分钟内未被消费的预密钥回退为 unused，
    // 避免发送方放弃/掉线导致预密钥池永久泄漏
    {
        QSqlQuery reclaimQ(db);
        if (!reclaimQ.exec(
                "UPDATE prekeys SET status = 'unused', claimed_at = NULL "
                "WHERE status = 'claimed' "
                "AND claimed_at <= datetime('now', '-600 seconds')")) {
            qWarning() << "[DB] claimPrekeys: failed to reclaim expired claims:"
                       << reclaimQ.lastError().text();
        }
    }

    // 有库存的设备列表
    QStringList deviceIds;
    {
        QSqlQuery q(db);
        q.prepare(
            "SELECT DISTINCT device_id FROM prekeys "
            "WHERE user_id = ? AND status = 'unused'");
        q.addBindValue(userId);
        if (q.exec()) {
            while (q.next()) {
                deviceIds.append(q.value(0).toString());
            }
        }
    }

    for (const QString &deviceId : deviceIds) {
        // 选出最早上传的一个未认领预密钥；并发竞争失败时跳过该设备，
        // 不影响其他设备的认领（瞬时失败不应拖垮整批）
        qint64 prekeyId = 0;
        QString pub;
        {
            QSqlQuery q(db);
            q.prepare(
                "SELECT id, pub FROM prekeys "
                "WHERE user_id = ? AND device_id = ? AND status = 'unused' "
                "ORDER BY id LIMIT 1");
            q.addBindValue(userId);
            q.addBindValue(deviceId);
            if (!q.exec() || !q.next()) {
                continue;
            }
            prekeyId = q.value(0).toLongLong();
            pub = q.value(1).toString();
        }

        // 条件更新：仅当仍为 unused 时认领成功（记录认领时间供超时回收）
        QSqlQuery updateQ(db);
        updateQ.prepare(
            "UPDATE prekeys SET status = 'claimed', claimed_at = datetime('now') "
            "WHERE id = ? AND status = 'unused'");
        updateQ.addBindValue(prekeyId);
        if (!updateQ.exec() || updateQ.numRowsAffected() != 1) {
            continue; // 被并发认领走，跳过该设备
        }

        ClaimedPrekey claimed;
        claimed.deviceId = deviceId;
        claimed.prekeyId = prekeyId;
        claimed.prekeyPub = pub;
        result.append(claimed);
    }

    if (!db.commit()) {
        db.rollback();
        qWarning() << "[DB] claimPrekeys failed, rolled back";
        return {};
    }
    return result;
}

// 手动事务包装（供调用方把多个写操作绑定为原子单元）
bool DatabaseManager::beginTransaction()
{
    return QSqlDatabase::database(m_connectionName).transaction();
}

bool DatabaseManager::commitTransaction()
{
    return QSqlDatabase::database(m_connectionName).commit();
}

bool DatabaseManager::rollbackTransaction()
{
    return QSqlDatabase::database(m_connectionName).rollback();
}

bool DatabaseManager::validateClaimedPrekey(qint64 userId, const QString &deviceId,
                                            qint64 prekeyId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(
        "SELECT COUNT(*) FROM prekeys "
        "WHERE id = ? AND user_id = ? AND device_id = ? AND status = 'claimed'");
    q.addBindValue(prekeyId);
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt() > 0;
    }
    return false;
}

int DatabaseManager::consumePrekeys(const QList<qint64> &prekeyIds)
{
    if (prekeyIds.isEmpty()) {
        return 0;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("UPDATE prekeys SET status = 'used' WHERE id = ? AND status = 'claimed'");
    int consumed = 0;
    for (qint64 id : prekeyIds) {
        q.bindValue(0, id);
        if (q.exec() && q.numRowsAffected() == 1) {
            ++consumed;
        }
    }
    return consumed;
}

bool DatabaseManager::removeDeviceKeys(qint64 userId, const QString &deviceId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    QSqlQuery q(db);
    q.prepare("DELETE FROM device_identity_keys WHERE user_id = ? AND device_id = ?");
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    if (!q.exec()) {
        qWarning() << "[DB] removeDeviceKeys (identity) failed:" << q.lastError().text();
        return false;
    }

    q.prepare("DELETE FROM prekeys WHERE user_id = ? AND device_id = ?");
    q.addBindValue(userId);
    q.addBindValue(deviceId);
    if (!q.exec()) {
        qWarning() << "[DB] removeDeviceKeys (prekeys) failed:" << q.lastError().text();
        return false;
    }
    return true;
}
