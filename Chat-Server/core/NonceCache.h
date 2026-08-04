#pragma once

#include <QString>
#include <QHash>
#include <QMutex>

// M5.5: 服务端全局 nonce 缓存（带 TTL）。
// 所有连接共享同一实例，避免"每连接内存集合"导致跨连接重放与重启清空问题；
// 条目在 TTL 过期后被惰性清理，总量受上限约束。
class NonceCache
{
public:
    explicit NonceCache(int ttlSeconds = 600, int maxEntries = 100000);

    // 检查并登记 nonce：返回 true 表示首次出现（允许请求），
    // 返回 false 表示重复（应拒绝请求）。空 nonce 一律拒绝。
    bool checkAndInsert(const QString &nonce);

    // 清理过期条目
    void purgeExpired();

    int size() const;

private:
    mutable QMutex m_mutex;
    QHash<QString, qint64> m_nonces; // nonce -> 首次出现的 Unix 秒
    int m_ttlSeconds;
    int m_maxEntries;
};
