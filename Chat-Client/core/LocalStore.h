#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QSqlDatabase>

/**
 * M6.5: 客户端本地持久化缓存（按账号 + 设备隔离）
 *
 * - SQLite 本地数据库：AppDataLocation/localstore/<username>_<deviceId>.db
 * - 消息正文与会话预览以 AES-256-GCM 加密后落库（存储密钥随机生成，
 *   经 KeyStorage DPAPI 保护），磁盘上不存在可读的消息明文
 * - 包含持久化 outbox、解密缓存（归口替代 M6 的 KeyStorage .cache 文件）
 *   与 sync_events 同步游标
 * - 登出时经 closeAndDestroy() 整体删除（E2EE 身份密钥不在此处，仍由
 *   KeyStorage 保留以便下次登录复用）
 */
class LocalStore
{
public:
    struct OutboxItem
    {
        QString clientMessageId;
        qint64 toUserId = 0;      // 私聊目标（群消息为 0）
        qint64 conversationId = 0; // M7a: 群聊目标会话（私聊为 0）
        QString content; // 明文正文（群消息为原文，私聊为待加密明文）
    };

    LocalStore() = default;
    ~LocalStore();
    LocalStore(const LocalStore &) = delete;
    LocalStore &operator=(const LocalStore &) = delete;

    // 打开/关闭。打开时加载（或首次生成）存储密钥并建表；
    // 密钥无法持久化时返回 false（fail-closed：宁可无缓存也不落明文）
    bool open(const QString &username, const QString &deviceId);
    // 仅关闭数据库（保留文件，供下次启动复用）
    void close();
    // 清除用户可见数据（消息/会话/outbox/同步游标），但保留解密缓存与
    // 存储密钥：二者属 E2EE 密钥材料：一次性预密钥消费后不可恢复，
    // 登出重登必须依靠解密缓存兜底（与 M6 产品承诺一致）
    bool clearUserData();
    // 关闭并删除本地数据库与存储密钥（彻底销毁，旧密文不可再恢复；
    // 仅用于不再需要重登解密的场景）
    void closeAndDestroy();
    bool isOpen() const { return m_open; }
    QString username() const { return m_username; }

    // 持久化 outbox（正文加密存储）；M7a: conversationId > 0 表示群消息
    bool addOutboxItem(const QString &clientMessageId, qint64 toUserId,
                       const QString &plaintext, qint64 conversationId = 0);
    bool removeOutboxItem(const QString &clientMessageId);
    QList<OutboxItem> loadOutbox() const;

    // 消息缓存（content 传入/返回均为明文，落库时加密）
    // msg 需含 messageId/conversationId/content 等 sync_messages 响应字段；
    // undecryptable=true 且已有可解密正文时保留旧明文不覆盖
    bool upsertMessage(const QJsonObject &msg);
    // 按 messageId 升序返回该会话最近 limit 条消息（字段同服务端响应）
    QJsonArray loadMessages(qint64 conversationId, int limit = 100) const;
    bool updateMessageStatus(qint64 messageId, const QString &status);

    // 会话缓存（lastMessage 预览加密存储）
    bool upsertConversation(const QJsonObject &conv);
    QJsonArray loadConversations() const;
    // 仅更新已存在的会话行（避免事件流缺字段时产生幻影会话）
    bool bumpConversationPreview(qint64 conversationId, const QString &preview,
                                 bool incrementUnread);

    // 解密缓存（messageId -> 明文，归口替代 M6 KeyStorage .cache）
    QString loadDecryptedContent(qint64 messageId) const;
    bool saveDecryptedContent(qint64 messageId, const QString &plaintext);
    // 一次性导入并删除 M6 遗留的 KeyStorage 解密缓存文件，返回导入条数
    int importLegacyDecryptCache(const QString &username, const QString &deviceId);

    // sync_events 增量同步游标
    qint64 syncCursor() const;
    bool setSyncCursor(qint64 seq);

    static QString dbFilePath(const QString &username, const QString &deviceId);

private:
    bool ensureSchema();
    // M7a: 列存在性检查（存量库幂等补列）
    bool hasColumn(const QString &table, const QString &column) const;
    bool ensureStorageKey(const QString &username, const QString &deviceId);
    // 历史缺陷自愈：旧版本曾把 envelope 密文误存为正文，打开时检出并
    // 清空为 undecryptable（正文由后续重新同步 + 解密缓存恢复）
    void healEnvelopeLeaks();
    // AES-256-GCM 文本加解密；格式 "enc1:<base64(iv)>:<base64(密文+标签)>"，
    // 解密失败（含格式不符）返回空：宁缺毋滥，绝不回退明文
    QString encryptText(const QString &plaintext) const;
    QString decryptText(const QString &cipher) const;
    QString loadMessageContent(qint64 messageId) const;
    void closeDatabase();

    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_username;
    QString m_deviceId;
    QByteArray m_storageKey; // 32 字节 AES-256-GCM 密钥，仅驻留进程内存
    bool m_open = false;
};
