#include "LocalStore.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

#include "KeyStorage.h"
#include "encryption/E2eeCrypto.h"
#include "security/SecureMemory.h"

using XYChat::Security::E2eeCrypto;
using XYChat::Security::SecureMemory;

namespace
{
constexpr int StorageKeySize = 32;
const QString CipherPrefix = "enc1:";

// QJsonValue::toString() 对缺失字段返回 null QString，Qt SQLite 驱动会将其
// 绑定为 SQL NULL；统一规范化为非 null 空串，避免 NOT NULL 约束失败
QString text(const QJsonObject &obj, const QString &key, const QString &fallback = QString())
{
    const QString value = obj.value(key).toString();
    if (!value.isNull()) {
        return value;
    }
    return fallback.isNull() ? QString("") : fallback;
}

// 消息状态只前进不回退的排序（sending/failed 视为最低）
int statusRank(const QString &status)
{
    if (status == "sent") return 1;
    if (status == "delivered") return 2;
    if (status == "read") return 3;
    return 0;
}
} // namespace

LocalStore::~LocalStore()
{
    if (m_open) {
        closeDatabase();
    }
    SecureMemory::wipe(m_storageKey);
}

QString LocalStore::dbFilePath(const QString &username, const QString &deviceId)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/localstore/";
    return dir + username + '_' + deviceId + ".db";
}

bool LocalStore::open(const QString &username, const QString &deviceId)
{
    if (m_open) {
        return true;
    }
    if (username.isEmpty() || deviceId.isEmpty()) {
        return false;
    }
    // 审查修复：驱动不可用时早退并给出明确日志（避免后续 addDatabase
    // 静默返回无效句柄，错误延后到首次 SQL 执行才暴露）
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        qWarning() << "[LocalStore] QSQLITE driver unavailable, local cache disabled";
        return false;
    }

    if (!ensureStorageKey(username, deviceId)) {
        qWarning() << "[LocalStore] Storage key unavailable, local cache disabled";
        return false;
    }

    m_username = username;
    m_deviceId = deviceId;
    if (!connectDatabase()) {
        SecureMemory::wipe(m_storageKey);
        m_storageKey.clear();
        m_username.clear();
        m_deviceId.clear();
        return false;
    }

    if (!ensureSchema()) {
        closeDatabase();
        return false;
    }

    // 历史缺陷自愈（必须在置 m_open 前完成，避免缓存先行展示泄漏行）
    healEnvelopeLeaks();

    m_open = true;
    return true;
}

bool LocalStore::connectDatabase()
{
    const QString path = dbFilePath(m_username, m_deviceId);
    QDir().mkpath(QFileInfo(path).absolutePath());

    // 每次打开使用独立连接名，避免登出销毁后重新打开命中陈旧句柄
    m_connectionName = "xychat_localstore_"
        + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        qWarning() << "[LocalStore] Failed to open database:" << db.lastError().text();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        return false;
    }
    m_db = db;

    {
        QSqlQuery pragma(m_db);
        pragma.exec("PRAGMA busy_timeout = 5000");
    }
    return true;
}

bool LocalStore::ensureUsableDb()
{
    if (!m_open) {
        return false;
    }
    if (m_db.isValid() && m_db.isOpen()) {
        return true;
    }

    // 连接意外失效（如陈旧句柄/驱动异常）：释放旧句柄后按原参数重开。
    // removeDatabase 前必须先释放全部 QSqlDatabase 拷贝，否则行为未定义
    qWarning() << "[LocalStore] Database connection lost, attempting to reopen";
    {
        QSqlDatabase stale = m_db;
        m_db = QSqlDatabase();
        if (stale.isOpen()) {
            stale.close();
        }
    }
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }

    if (!connectDatabase() || !ensureSchema()) {
        qWarning() << "[LocalStore] Reopen failed, local cache disabled";
        closeDatabase();
        return false;
    }
    healEnvelopeLeaks();
    return true;
}

void LocalStore::close()
{
    if (m_open) {
        closeDatabase();
    }
}

bool LocalStore::clearUserData()
{
    if (!ensureUsableDb()) {
        return false;
    }
    // 只清除用户可见数据；decrypt_cache 与存储密钥属 E2EE 密钥材料，
    // 登出重登时预密钥已消费不可恢复，必须保留供解密兜底
    const QStringList tables = {
        "messages",
        "conversations",
        "outbox",
        "meta",
    };
    bool ok = true;
    QSqlQuery query(m_db);
    for (const QString &table : tables) {
        if (!query.exec("DELETE FROM " + table)) {
            qWarning() << "[LocalStore] clearUserData failed on" << table
                       << query.lastError().text();
            ok = false;
        }
    }
    return ok;
}

void LocalStore::closeAndDestroy()
{
    if (!m_open) {
        return;
    }

    const QString path = dbFilePath(m_username, m_deviceId);
    const QString username = m_username;
    const QString deviceId = m_deviceId;
    closeDatabase();

    // 登出清除本地数据：数据库文件（含 WAL/journal 残留）与存储密钥一并删除，
    // 旧密文失去解密能力。E2EE 身份密钥不在此列（KeyStorage 保留复用）
    QFile::remove(path);
    QFile::remove(path + "-journal");
    QFile::remove(path + "-wal");
    QFile::remove(path + "-shm");
    KeyStorage::removeLocalStoreKey(username, deviceId);
    KeyStorage::removeDecryptCacheFile(username, deviceId);
}

void LocalStore::closeDatabase()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
    SecureMemory::wipe(m_storageKey);
    m_storageKey.clear();
    m_username.clear();
    m_deviceId.clear();
    m_open = false;
}

bool LocalStore::ensureStorageKey(const QString &username, const QString &deviceId)
{
    QByteArray key = KeyStorage::loadLocalStoreKey(username, deviceId);
    if (key.size() != StorageKeySize) {
        SecureMemory::wipe(key);
        // 审查修复：区分“密钥文件不存在”与“DPAPI 还原失败”。
        // 还原失败（如凭据迁移/变更）时用新密钥覆盖会使既有加密库永久
        // 不可解，宁可禁用缓存也不能销毁旧密文的解密能力
        if (QFile::exists(KeyStorage::localStoreKeyFilePath(username, deviceId))) {
            qWarning() << "[LocalStore] Existing storage key could not be restored,"
                          "cache disabled to protect existing ciphertext";
            return false;
        }
        key = E2eeCrypto::generateRandomBytes(StorageKeySize);
        if (key.size() != StorageKeySize) {
            return false;
        }
        // fail-closed：密钥无法持久化则不启用缓存（否则重启后旧库不可读，
        // 也不允许任何明文回退路径）
        if (!KeyStorage::saveLocalStoreKey(username, deviceId, key)) {
            qWarning() << "[LocalStore] Failed to persist storage key, cache disabled";
            SecureMemory::wipe(key);
            return false;
        }
    }
    SecureMemory::wipe(m_storageKey);
    m_storageKey = key;
    return true;
}

bool LocalStore::ensureSchema()
{
    const QStringList statements = {
        "CREATE TABLE IF NOT EXISTS schema_meta ("
                       "version INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS messages ("
                       "message_id INTEGER PRIMARY KEY,"
                       "conversation_id INTEGER NOT NULL,"
                       "sender_id INTEGER NOT NULL DEFAULT 0,"
                       "sender_username TEXT NOT NULL DEFAULT '',"
                       "content_enc TEXT NOT NULL DEFAULT '',"
                       "content_type TEXT NOT NULL DEFAULT 'text',"
                       "status TEXT NOT NULL DEFAULT 'sent',"
                       "status_rank INTEGER NOT NULL DEFAULT 0,"
                       "undecryptable INTEGER NOT NULL DEFAULT 0,"
                       "client_message_id TEXT NOT NULL DEFAULT '',"
                       "created_at TEXT NOT NULL DEFAULT '')",
        "CREATE INDEX IF NOT EXISTS idx_messages_conv "
                       "ON messages(conversation_id, message_id)",
        "CREATE TABLE IF NOT EXISTS conversations ("
                       "conversation_id INTEGER PRIMARY KEY,"
                       "type TEXT NOT NULL DEFAULT 'private',"
                       "peer_user_id INTEGER NOT NULL DEFAULT 0,"
                       "peer_username TEXT NOT NULL DEFAULT '',"
                       "last_message_enc TEXT NOT NULL DEFAULT '',"
                       "last_message_id INTEGER NOT NULL DEFAULT 0,"
                       "last_message_at TEXT NOT NULL DEFAULT '',"
                       "unread_count INTEGER NOT NULL DEFAULT 0,"
                       "name TEXT NOT NULL DEFAULT '',"
                       "member_count INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS outbox ("
                       "client_message_id TEXT PRIMARY KEY,"
                       "to_user_id INTEGER NOT NULL,"
                       "content_enc TEXT NOT NULL,"
                       "created_at TEXT NOT NULL DEFAULT '',"
                       "conversation_id INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS decrypt_cache ("
                       "message_id INTEGER PRIMARY KEY,"
                       "content_enc TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS meta ("
                       "key TEXT PRIMARY KEY,"
                       "value TEXT NOT NULL)",
    };

    QSqlQuery query(m_db);
    for (const QString &sql : statements) {
        if (!query.exec(sql)) {
            qWarning() << "[LocalStore] Schema failed:" << query.lastError().text();
            return false;
        }
    }

    // M7a: 存量库幂等补列（群名/成员数/群 outbox 目标）
    if (!hasColumn("conversations", "name")
        && !query.exec("ALTER TABLE conversations "
                       "ADD COLUMN name TEXT NOT NULL DEFAULT ''")) {
        qWarning() << "[LocalStore] Add conversations.name failed:" << query.lastError().text();
        return false;
    }
    if (!hasColumn("conversations", "member_count")
        && !query.exec("ALTER TABLE conversations "
                       "ADD COLUMN member_count INTEGER NOT NULL DEFAULT 0")) {
        qWarning() << "[LocalStore] Add conversations.member_count failed:" << query.lastError().text();
        return false;
    }
    if (!hasColumn("outbox", "conversation_id")
        && !query.exec("ALTER TABLE outbox "
                       "ADD COLUMN conversation_id INTEGER NOT NULL DEFAULT 0")) {
        qWarning() << "[LocalStore] Add outbox.conversation_id failed:" << query.lastError().text();
        return false;
    }

    // 记录 schema 版本（首次插入）
    query.exec("INSERT INTO schema_meta(version) "
                              "SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM schema_meta)");
    return true;
}

bool LocalStore::hasColumn(const QString &table, const QString &column) const
{
    QSqlQuery query(m_db);
    if (!query.exec("PRAGMA table_info(" + table + ")")) {
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == column) {
            return true;
        }
    }
    return false;
}

// 文本加解密

QString LocalStore::encryptText(const QString &plaintext) const
{
    if (plaintext.isEmpty() || m_storageKey.size() != StorageKeySize) {
        return {};
    }
    const auto gcm = E2eeCrypto::aesGcmEncrypt(m_storageKey, plaintext.toUtf8());
    if (!gcm.valid) {
        return {};
    }
    return CipherPrefix + QString::fromLatin1(gcm.iv.toBase64())
        + QLatin1Char(':') + QString::fromLatin1(gcm.ciphertext.toBase64());
}

QString LocalStore::decryptText(const QString &cipher) const
{
    if (!cipher.startsWith(CipherPrefix) || m_storageKey.size() != StorageKeySize) {
        return {};
    }
    const QStringList parts = cipher.mid(CipherPrefix.size()).split(QLatin1Char(':'));
    if (parts.size() != 2) {
        return {};
    }
    const QByteArray iv = QByteArray::fromBase64(
        parts.at(0).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    const QByteArray ct = QByteArray::fromBase64(
        parts.at(1).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (iv.isEmpty() || ct.isEmpty()) {
        return {};
    }
    const QByteArray plain = E2eeCrypto::aesGcmDecrypt(m_storageKey, iv, ct);
    return QString::fromUtf8(plain);
}

// 持久化 outbox

bool LocalStore::addOutboxItem(const QString &clientMessageId, qint64 toUserId,
                               const QString &plaintext, qint64 conversationId)
{
    // 私聊需有效接收者；群聊需有效会话 ID（两者至少一个）
    if (!ensureUsableDb() || clientMessageId.isEmpty()
        || (toUserId <= 0 && conversationId <= 0)) {
        return false;
    }
    const QString enc = encryptText(plaintext);
    if (enc.isEmpty()) {
        qWarning() << "[LocalStore] Refusing to cache outbox item without encryption";
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT OR REPLACE INTO outbox"
        "(client_message_id, to_user_id, content_enc, created_at, conversation_id) "
        "VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(clientMessageId);
    query.addBindValue(toUserId);
    query.addBindValue(enc);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(conversationId);
    if (!query.exec()) {
        qWarning() << "[LocalStore] addOutboxItem failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool LocalStore::removeOutboxItem(const QString &clientMessageId)
{
    if (!ensureUsableDb() || clientMessageId.isEmpty()) {
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM outbox WHERE client_message_id = ?");
    query.addBindValue(clientMessageId);
    return query.exec();
}

QList<LocalStore::OutboxItem> LocalStore::loadOutbox() const
{
    QList<OutboxItem> result;
    if (!m_open) {
        return result;
    }
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT client_message_id, to_user_id, content_enc, conversation_id "
        "FROM outbox ORDER BY created_at");
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        OutboxItem item;
        item.clientMessageId = query.value(0).toString();
        item.toUserId = query.value(1).toLongLong();
        item.content = decryptText(query.value(2).toString());
        item.conversationId = query.value(3).toLongLong();
        const bool validTarget = item.toUserId > 0 || item.conversationId > 0;
        if (!item.clientMessageId.isEmpty() && validTarget && !item.content.isEmpty()) {
            result.append(item);
        }
    }
    return result;
}

// 消息缓存

QString LocalStore::loadMessageContent(qint64 messageId) const
{
    if (!m_open || messageId <= 0) {
        return {};
    }
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT content_enc, undecryptable FROM messages WHERE message_id = ?");
    query.addBindValue(messageId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    if (query.value(1).toInt() != 0) {
        return {};
    }
    return decryptText(query.value(0).toString());
}

bool LocalStore::upsertMessage(const QJsonObject &msg)
{
    if (!ensureUsableDb()) {
        return false;
    }
    const qint64 messageId = msg.value("messageId").toVariant().toLongLong();
    const qint64 conversationId = msg.value("conversationId").toVariant().toLongLong();
    if (messageId <= 0 || conversationId <= 0) {
        return false;
    }

    const bool undecryptable = msg.value("undecryptable").toBool();
    QString content = msg.value("content").toString();
    // 修复：envelope 原文不是明文，绝不落库（否则密文会伪装成正文泄漏到
    // UI）；解密失败时 content 仍残留 envelope，统一清空并标记 undecryptable
    bool markUndecryptable = undecryptable;
    if (undecryptable || E2eeCrypto::looksLikeEnvelope(content)) {
        content.clear();
        markUndecryptable = true;
    }
    if (content.isEmpty()) {
        // 已解密正文优先：重新同步命中缓存前先不覆盖既有条目
        const QString existing = loadMessageContent(messageId);
        if (!existing.isEmpty()) {
            return true;
        }
    }

    QString enc;
    if (!content.isEmpty()) {
        enc = encryptText(content);
        if (enc.isEmpty()) {
            qWarning() << "[LocalStore] Refusing to cache message without encryption";
            return false;
        }
    }

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO messages(message_id, conversation_id, sender_id, sender_username,"
        " content_enc, content_type, status, status_rank, undecryptable, client_message_id, created_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(message_id) DO UPDATE SET"
        " conversation_id = excluded.conversation_id,"
        " sender_id = excluded.sender_id,"
        " sender_username = excluded.sender_username,"
        " content_enc = CASE WHEN excluded.content_enc != '' THEN excluded.content_enc"
        "                    ELSE messages.content_enc END,"
        " content_type = excluded.content_type,"
        // 审查修复：回执状态只前进不回退，滞后同步不得把 read 退回 delivered
        " status = CASE WHEN excluded.content_enc != ''"
        "                    AND excluded.status_rank >= messages.status_rank"
        "               THEN excluded.status ELSE messages.status END,"
        " status_rank = CASE WHEN excluded.content_enc != ''"
        "                         AND excluded.status_rank >= messages.status_rank"
        "                    THEN excluded.status_rank ELSE messages.status_rank END,"
        " undecryptable = CASE WHEN excluded.content_enc != '' THEN 0"
        "                      ELSE messages.undecryptable END,"
        " client_message_id = excluded.client_message_id,"
        " created_at = excluded.created_at");
    query.addBindValue(messageId);
    query.addBindValue(conversationId);
    query.addBindValue(msg.value("senderId").toVariant().toLongLong());
    query.addBindValue(text(msg, "senderUsername"));
    query.addBindValue(enc.isEmpty() ? QString("") : enc);
    query.addBindValue(text(msg, "contentType", "text"));
    query.addBindValue(text(msg, "status", "sent"));
    query.addBindValue(markUndecryptable && content.isEmpty() ? 1 : 0);
    query.addBindValue(text(msg, "clientMessageId"));
    query.addBindValue(text(msg, "createdAt"));
    query.addBindValue(statusRank(msg.value("status").toString()));
    if (!query.exec()) {
        qWarning() << "[LocalStore] upsertMessage failed:" << query.lastError().text();
        return false;
    }

    // 解密缓存归口：可解密正文同时写入 decrypt_cache，供后续 envelope 命中
    if (!content.isEmpty()) {
        saveDecryptedContent(messageId, content);
    }
    return true;
}

QJsonArray LocalStore::loadMessages(qint64 conversationId, int limit) const
{
    QJsonArray result;
    if (!m_open || conversationId <= 0 || limit <= 0) {
        return result;
    }
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT message_id, conversation_id, sender_id, sender_username, content_enc,"
        " content_type, status, created_at"
        " FROM messages WHERE conversation_id = ? ORDER BY message_id DESC LIMIT ?");
    query.addBindValue(conversationId);
    query.addBindValue(limit);
    if (!query.exec()) {
        return result;
    }

    // 查询按 messageId 降序，输出翻转为升序
    QList<QJsonObject> rows;
    while (query.next()) {
        QJsonObject msg;
        msg["messageId"] = query.value(0).toLongLong();
        msg["conversationId"] = query.value(1).toLongLong();
        msg["senderId"] = query.value(2).toLongLong();
        msg["senderUsername"] = query.value(3).toString();
        // 密文解密失败（密钥不匹配/条目缺失）时标记 undecryptable，
        // 与实时接收路径的消息形状保持一致
        const QString content = decryptText(query.value(4).toString());
        if (!content.isEmpty()) {
            msg["content"] = content;
        } else {
            msg["content"] = QString();
            msg["undecryptable"] = true;
        }
        msg["contentType"] = query.value(5).toString();
        msg["status"] = query.value(6).toString();
        msg["createdAt"] = query.value(7).toString();
        rows.append(msg);
    }
    for (int i = rows.size() - 1; i >= 0; --i) {
        result.append(rows.at(i));
    }
    return result;
}

bool LocalStore::updateMessageStatus(qint64 messageId, const QString &status)
{
    if (!ensureUsableDb() || messageId <= 0 || status.isEmpty()) {
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(
        "UPDATE messages SET status = ?, status_rank = ? WHERE message_id = ?");
    query.addBindValue(status);
    query.addBindValue(statusRank(status));
    query.addBindValue(messageId);
    return query.exec();
}

// 会话缓存

bool LocalStore::upsertConversation(const QJsonObject &conv)
{
    if (!ensureUsableDb()) {
        return false;
    }
    const qint64 conversationId = conv.value("conversationId").toVariant().toLongLong();
    if (conversationId <= 0) {
        return false;
    }

    const QString lastMessage = conv.value("lastMessage").toString();
    QString enc;
    if (!lastMessage.isEmpty()) {
        enc = encryptText(lastMessage);
        if (enc.isEmpty()) {
            qWarning() << "[LocalStore] Refusing to cache conversation preview without encryption";
            return false;
        }
    }

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO conversations(conversation_id, type, peer_user_id, peer_username,"
        " last_message_enc, last_message_id, last_message_at, unread_count,"
        " name, member_count)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(conversation_id) DO UPDATE SET"
        " type = excluded.type,"
        " peer_user_id = excluded.peer_user_id,"
        " peer_username = excluded.peer_username,"
        " last_message_enc = excluded.last_message_enc,"
        " last_message_id = excluded.last_message_id,"
        " last_message_at = excluded.last_message_at,"
        " unread_count = excluded.unread_count,"
        " name = excluded.name,"
        " member_count = excluded.member_count");
    query.addBindValue(conversationId);
    query.addBindValue(text(conv, "type", "private"));
    query.addBindValue(conv.value("peerUserId").toVariant().toLongLong());
    query.addBindValue(text(conv, "peerUsername"));
    query.addBindValue(enc.isEmpty() ? QString("") : enc);
    query.addBindValue(conv.value("lastMessageId").toVariant().toLongLong());
    query.addBindValue(text(conv, "lastMessageAt"));
    query.addBindValue(conv.value("unreadCount").toInt());
    // M7a: 群会话字段（private 会话为空/0）
    query.addBindValue(text(conv, "name"));
    query.addBindValue(conv.value("memberCount").toInt());
    if (!query.exec()) {
        qWarning() << "[LocalStore] upsertConversation failed:" << query.lastError().text();
        return false;
    }
    return true;
}

QJsonArray LocalStore::loadConversations() const
{
    QJsonArray result;
    if (!m_open) {
        return result;
    }
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT conversation_id, type, peer_user_id, peer_username, last_message_enc,"
        " last_message_id, last_message_at, unread_count, name, member_count"
        " FROM conversations ORDER BY last_message_at DESC, conversation_id DESC");
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        QJsonObject conv;
        conv["conversationId"] = query.value(0).toLongLong();
        conv["type"] = query.value(1).toString();
        conv["peerUserId"] = query.value(2).toLongLong();
        conv["peerUsername"] = query.value(3).toString();
        conv["lastMessage"] = decryptText(query.value(4).toString());
        conv["lastMessageId"] = query.value(5).toLongLong();
        conv["lastMessageAt"] = query.value(6).toString();
        conv["unreadCount"] = query.value(7).toInt();
        conv["name"] = query.value(8).toString();
        conv["memberCount"] = query.value(9).toInt();
        result.append(conv);
    }
    return result;
}

bool LocalStore::bumpConversationPreview(qint64 conversationId, const QString &preview,
                                         bool incrementUnread)
{
    if (!ensureUsableDb() || conversationId <= 0) {
        return false;
    }
    QString enc;
    if (!preview.isEmpty()) {
        enc = encryptText(preview);
        if (enc.isEmpty()) {
            return false;
        }
    }
    QSqlQuery query(m_db);
    query.prepare(
        "UPDATE conversations SET last_message_enc = ?, last_message_at = ?,"
        " unread_count = unread_count + ? WHERE conversation_id = ?");
    query.addBindValue(enc.isEmpty() ? QString("") : enc);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(incrementUnread ? 1 : 0);
    query.addBindValue(conversationId);
    return query.exec();
}

// 解密缓存

QString LocalStore::loadDecryptedContent(qint64 messageId) const
{
    if (!m_open || messageId <= 0) {
        return {};
    }
    QSqlQuery query(m_db);
    query.prepare("SELECT content_enc FROM decrypt_cache WHERE message_id = ?");
    query.addBindValue(messageId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return decryptText(query.value(0).toString());
}

bool LocalStore::saveDecryptedContent(qint64 messageId, const QString &plaintext)
{
    if (!ensureUsableDb() || messageId <= 0 || plaintext.isEmpty()) {
        return false;
    }
    const QString enc = encryptText(plaintext);
    if (enc.isEmpty()) {
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT OR REPLACE INTO decrypt_cache(message_id, content_enc) VALUES (?, ?)");
    query.addBindValue(messageId);
    query.addBindValue(enc);
    return query.exec();
}

int LocalStore::importLegacyDecryptCache(const QString &username, const QString &deviceId)
{
    if (!m_open) {
        return 0;
    }
    const auto legacy = KeyStorage::loadDecryptCache(username, deviceId);
    if (legacy.isEmpty()) {
        return 0;
    }
    int imported = 0;
    for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) {
        if (saveDecryptedContent(it.key(), it.value())) {
            ++imported;
        }
    }
    // 迁移完成后删除遗留文件，避免双份缓存漂移
    KeyStorage::removeDecryptCacheFile(username, deviceId);
    qInfo() << "[LocalStore] Imported" << imported << "entries from legacy decrypt cache";
    return imported;
}

// 同步游标

qint64 LocalStore::syncCursor() const
{
    if (!m_open) {
        return 0;
    }
    QSqlQuery query(m_db);
    query.prepare("SELECT value FROM meta WHERE key = 'sync_seq'");
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toString().toLongLong();
}

bool LocalStore::setSyncCursor(qint64 seq)
{
    if (!ensureUsableDb() || seq < 0) {
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO meta(key, value) VALUES ('sync_seq', ?)"
        " ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    query.addBindValue(QString::number(seq));
    return query.exec();
}

void LocalStore::healEnvelopeLeaks()
{
    // 旧版本缺陷曾把解密失败的 envelope 原文当作正文落库；打开时扫描并
    // 清空为 undecryptable，正文由后续重新同步 + 解密缓存恢复
    QSqlQuery select(m_db);
    if (!select.exec(
            "SELECT message_id, content_enc FROM messages WHERE content_enc != ''")) {
        return;
    }
    QList<qint64> leaked;
    while (select.next()) {
        const QString plain = decryptText(select.value(1).toString());
        if (!plain.isEmpty() && E2eeCrypto::looksLikeEnvelope(plain)) {
            leaked.append(select.value(0).toLongLong());
        }
    }
    if (leaked.isEmpty()) {
        return;
    }
    for (const qint64 messageId : leaked) {
        QSqlQuery update(m_db);
        update.prepare(
            "UPDATE messages SET content_enc = '', undecryptable = 1 WHERE message_id = ?");
        update.addBindValue(messageId);
        update.exec();
    }
    qInfo() << "[LocalStore] Healed" << leaked.size() << "leaked envelope row(s)";
}
