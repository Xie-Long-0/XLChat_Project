#include "NonceCache.h"

#include <QDateTime>
#include <QMutexLocker>

NonceCache::NonceCache(int ttlSeconds, int maxEntries)
    : m_ttlSeconds(ttlSeconds)
    , m_maxEntries(maxEntries)
{
}

bool NonceCache::checkAndInsert(const QString &nonce)
{
    if (nonce.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&m_mutex);
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // 惰性清理过期条目，控制内存占用
    if (m_nonces.size() > m_maxEntries / 2) {
        for (auto it = m_nonces.begin(); it != m_nonces.end();) {
            if (now - it.value() > m_ttlSeconds) {
                it = m_nonces.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto it = m_nonces.constFind(nonce);
    if (it != m_nonces.constEnd()) {
        // 仍在 TTL 窗口内则判定为重放；已过期则允许重新使用
        if (now - it.value() <= m_ttlSeconds) {
            return false;
        }
    }

    // 达到硬上限时丢弃最旧条目（简单实现：整体清理一半）
    if (m_nonces.size() >= m_maxEntries) {
        qint64 cutoff = now - m_ttlSeconds / 2;
        for (auto jt = m_nonces.begin(); jt != m_nonces.end();) {
            if (jt.value() < cutoff) {
                jt = m_nonces.erase(jt);
            } else {
                ++jt;
            }
        }
    }

    m_nonces.insert(nonce, now);
    return true;
}

void NonceCache::purgeExpired()
{
    QMutexLocker locker(&m_mutex);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = m_nonces.begin(); it != m_nonces.end();) {
        if (now - it.value() > m_ttlSeconds) {
            it = m_nonces.erase(it);
        } else {
            ++it;
        }
    }
}

int NonceCache::size() const
{
    QMutexLocker locker(&m_mutex);
    return m_nonces.size();
}
