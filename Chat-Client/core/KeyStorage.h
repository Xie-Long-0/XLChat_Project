#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

/**
 * M6: 客户端 E2EE 密钥本地存储
 *
 * - 身份密钥对与未使用的一次性预密钥私钥持久化于
 *   AppDataLocation/e2ee/<username>_<deviceId>.key
 * - Windows 下文件内容经 DPAPI（CryptProtectData）按当前用户加密保护；
 *   其他平台明文回退并输出告警
 * - 私钥永不离开本机
 */
class KeyStorage
{
public:
    struct PrekeyEntry
    {
        QByteArray publicKey;
        QByteArray privateKey;
    };

    // 密钥文件路径（按账号 + 设备隔离）
    static QString keyFilePath(const QString &username, const QString &deviceId);

    // 身份密钥私钥读写（空表示尚未生成）
    static QByteArray loadIdentityPrivateKey(const QString &username, const QString &deviceId);
    static bool saveIdentityPrivateKey(const QString &username, const QString &deviceId,
                                       const QByteArray &privateKey);

    // 一次性预密钥读写（覆盖式保存）
    static QList<PrekeyEntry> loadPrekeys(const QString &username, const QString &deviceId);
    static bool savePrekeys(const QString &username, const QString &deviceId,
                            const QList<PrekeyEntry> &prekeys);

    // ── TOFU 信任存储：对方用户身份公钥指纹 ────────────────────────────────
    // 返回该用户已记录的指纹，无记录返回空
    static QString loadPeerFingerprint(qint64 peerUserId);
    // 记录/更新指纹
    static bool savePeerFingerprint(qint64 peerUserId, const QString &fingerprint);

    // ── 解密缓存：messageId -> 已解密正文 ─────────────────────────────────────
    // 一次性预密钥解密后即删除，重新登录/重新同步时依靠此缓存避免
    // 已读消息变为“无法解密”；同样经 DPAPI 保护
    static QHash<qint64, QString> loadDecryptCache(const QString &username,
                                                   const QString &deviceId);
    static bool saveDecryptCache(const QString &username, const QString &deviceId,
                                 const QHash<qint64, QString> &cache);
    // M6.5：删除遗留解密缓存文件（新缓存已归口 LocalStore，迁移后调用）
    static bool removeDecryptCacheFile(const QString &username, const QString &deviceId);

    // ── M6.5：LocalStore 存储密钥（32 字节 AES-256-GCM 密钥，DPAPI 保护） ──
    // 文件：AppDataLocation/localstore/<username>_<deviceId>.key
    static QByteArray loadLocalStoreKey(const QString &username, const QString &deviceId);
    static bool saveLocalStoreKey(const QString &username, const QString &deviceId,
                                  const QByteArray &key);
    // 登出清除本地数据时删除存储密钥（旧密文不可再恢复）
    static bool removeLocalStoreKey(const QString &username, const QString &deviceId);
    // 存储密钥文件路径（供 LocalStore 判断“文件缺失”与“还原失败”）
    static QString localStoreKeyFilePath(const QString &username, const QString &deviceId);

private:
    // DPAPI 保护/还原（非 Windows 原样返回）
    static QByteArray protect(const QByteArray &data);
    static QByteArray unprotect(const QByteArray &data);
    static QString trustFilePath();
    static QString decryptCacheFilePath(const QString &username, const QString &deviceId);
};
