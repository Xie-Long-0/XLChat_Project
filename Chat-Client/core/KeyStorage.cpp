#include "KeyStorage.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "SecureMemory.h"

using XYChat::Security::SecureMemory;

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace
{

// 审查修复：先写临时文件再替换目标，避免写入中途崩溃留下半截密钥文件
bool writeFileAtomic(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString tmpPath = path + QStringLiteral(".tmp");

    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const bool written = tmp.write(data) == data.size();
    tmp.close();
    if (!written) {
        QFile::remove(tmpPath);
        return false;
    }

    if (QFile::exists(path)) {
        QFile::remove(path);
    }
    if (!QFile::rename(tmpPath, path)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

QByteArray readFileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = file.readAll();
    file.close();
    return data;
}

} // namespace

// ── DPAPI 保护（Windows）/ 原样回退（其他平台） ──────────────────────────────

QByteArray KeyStorage::protect(const QByteArray &data)
{
#ifdef Q_OS_WIN
    DATA_BLOB in;
    in.pbData = const_cast<BYTE *>(reinterpret_cast<const BYTE *>(data.constData()));
    in.cbData = static_cast<DWORD>(data.size());

    DATA_BLOB out;
    if (!CryptProtectData(&in, L"XYChat E2EE Keys", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    QByteArray result(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
#else
    qWarning() << "[KeyStorage] DPAPI unavailable on this platform; keys stored unprotected";
    return data;
#endif
}

QByteArray KeyStorage::unprotect(const QByteArray &data)
{
#ifdef Q_OS_WIN
    DATA_BLOB in;
    in.pbData = const_cast<BYTE *>(reinterpret_cast<const BYTE *>(data.constData()));
    in.cbData = static_cast<DWORD>(data.size());

    DATA_BLOB out;
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    QByteArray result(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
#else
    return data;
#endif
}

// ── 路径 ─────────────────────────────────────────────────────────────────────

QString KeyStorage::keyFilePath(const QString &username, const QString &deviceId)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/e2ee");
    return dir + QStringLiteral("/") + username + QStringLiteral("_") + deviceId
        + QStringLiteral(".key");
}

QString KeyStorage::trustFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/e2ee/trust.json");
}

QString KeyStorage::decryptCacheFilePath(const QString &username, const QString &deviceId)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/e2ee");
    return dir + QStringLiteral("/") + username + QStringLiteral("_") + deviceId
        + QStringLiteral(".cache");
}

// ── 身份密钥 ──

QByteArray KeyStorage::loadIdentityPrivateKey(const QString &username, const QString &deviceId)
{
    QByteArray raw = unprotect(readFileBytes(keyFilePath(username, deviceId)));
    if (raw.isEmpty()) {
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    const QByteArray priv =
        QByteArray::fromBase64(root.value("identityPriv").toString().toLatin1());
    SecureMemory::wipe(raw); // DPAPI 解密出的明文 blob 含私钥，及时清零
    return priv;
}

bool KeyStorage::saveIdentityPrivateKey(const QString &username, const QString &deviceId,
                                        const QByteArray &privateKey)
{
    // 读取现有文件以保留预密钥
    QJsonObject root;
    QByteArray existing = unprotect(readFileBytes(keyFilePath(username, deviceId)));
    if (!existing.isEmpty()) {
        root = QJsonDocument::fromJson(existing).object();
        SecureMemory::wipe(existing);
    }
    root["identityPriv"] = QString::fromLatin1(privateKey.toBase64());

    QByteArray blob = protect(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (blob.isEmpty()) {
        return false;
    }
    const bool ok = writeFileAtomic(keyFilePath(username, deviceId), blob);
    SecureMemory::wipe(blob);
    return ok;
}

// ── 预密钥 ──

QList<KeyStorage::PrekeyEntry> KeyStorage::loadPrekeys(const QString &username,
                                                       const QString &deviceId)
{
    QList<PrekeyEntry> result;
    QByteArray raw = unprotect(readFileBytes(keyFilePath(username, deviceId)));
    if (raw.isEmpty()) {
        return result;
    }
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    SecureMemory::wipe(raw);
    const QJsonArray array = root.value("prekeys").toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        PrekeyEntry entry;
        entry.publicKey = QByteArray::fromBase64(obj.value("pub").toString().toLatin1());
        entry.privateKey = QByteArray::fromBase64(obj.value("priv").toString().toLatin1());
        if (!entry.publicKey.isEmpty() && !entry.privateKey.isEmpty()) {
            result.append(entry);
        }
    }
    return result;
}

bool KeyStorage::savePrekeys(const QString &username, const QString &deviceId,
                             const QList<PrekeyEntry> &prekeys)
{
    // 读取现有文件以保留身份密钥
    QJsonObject root;
    QByteArray existing = unprotect(readFileBytes(keyFilePath(username, deviceId)));
    if (!existing.isEmpty()) {
        root = QJsonDocument::fromJson(existing).object();
        SecureMemory::wipe(existing);
    }

    QJsonArray array;
    for (const PrekeyEntry &entry : prekeys) {
        QJsonObject obj;
        obj["pub"] = QString::fromLatin1(entry.publicKey.toBase64());
        obj["priv"] = QString::fromLatin1(entry.privateKey.toBase64());
        array.append(obj);
    }
    root["prekeys"] = array;

    QByteArray blob = protect(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (blob.isEmpty()) {
        return false;
    }
    const bool ok = writeFileAtomic(keyFilePath(username, deviceId), blob);
    SecureMemory::wipe(blob);
    return ok;
}

// ── TOFU 信任存储 ──

QString KeyStorage::loadPeerFingerprint(qint64 peerUserId)
{
    const QJsonObject root = QJsonDocument::fromJson(readFileBytes(trustFilePath())).object();
    return root.value(QString::number(peerUserId)).toString();
}

bool KeyStorage::savePeerFingerprint(qint64 peerUserId, const QString &fingerprint)
{
    QJsonObject root = QJsonDocument::fromJson(readFileBytes(trustFilePath())).object();
    root[QString::number(peerUserId)] = fingerprint;
    return writeFileAtomic(trustFilePath(),
                           QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ── 解密缓存 ──

QHash<qint64, QString> KeyStorage::loadDecryptCache(const QString &username,
                                                    const QString &deviceId)
{
    QHash<qint64, QString> result;
    QByteArray raw = unprotect(readFileBytes(decryptCacheFilePath(username, deviceId)));
    if (raw.isEmpty()) {
        return result;
    }
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    SecureMemory::wipe(raw);
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        bool ok = false;
        const qint64 messageId = it.key().toLongLong(&ok);
        if (ok && messageId > 0) {
            result.insert(messageId, it.value().toString());
        }
    }
    return result;
}

bool KeyStorage::saveDecryptCache(const QString &username, const QString &deviceId,
                                  const QHash<qint64, QString> &cache)
{
    QJsonObject root;
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it) {
        root[QString::number(it.key())] = it.value();
    }
    QByteArray blob = protect(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (blob.isEmpty()) {
        return false;
    }
    const bool ok = writeFileAtomic(decryptCacheFilePath(username, deviceId), blob);
    SecureMemory::wipe(blob);
    return ok;
}

bool KeyStorage::removeDecryptCacheFile(const QString &username, const QString &deviceId)
{
    return QFile::remove(decryptCacheFilePath(username, deviceId));
}

// ── M6.5: LocalStore 存储密钥 ────────────────────────────────────────

QString KeyStorage::localStoreKeyFilePath(const QString &username, const QString &deviceId)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/localstore/";
    return dir + username + '_' + deviceId + ".key";
}

QByteArray KeyStorage::loadLocalStoreKey(const QString &username, const QString &deviceId)
{
    QByteArray raw = unprotect(readFileBytes(localStoreKeyFilePath(username, deviceId)));
    if (raw.isEmpty()) {
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    const QByteArray key =
        QByteArray::fromBase64(root.value("storageKey").toString().toLatin1());
    SecureMemory::wipe(raw);
    return key;
}

bool KeyStorage::saveLocalStoreKey(const QString &username, const QString &deviceId,
                                   const QByteArray &key)
{
    QJsonObject root;
    root["storageKey"] = QString::fromLatin1(key.toBase64());
    QByteArray blob = protect(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (blob.isEmpty()) {
        return false;
    }
    const bool ok = writeFileAtomic(localStoreKeyFilePath(username, deviceId), blob);
    SecureMemory::wipe(blob);
    return ok;
}

bool KeyStorage::removeLocalStoreKey(const QString &username, const QString &deviceId)
{
    return QFile::remove(localStoreKeyFilePath(username, deviceId));
}
