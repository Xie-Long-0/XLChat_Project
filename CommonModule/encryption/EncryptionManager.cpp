#include "EncryptionManager.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <QStringList>

// M1 兼容：SHA-256
QString EncryptionManager::encryptPassword(const QString &password)
{
    QByteArray passwordBytes = password.toUtf8();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(passwordBytes.data()),
           passwordBytes.size(), hash);

    QByteArray hashBytes(reinterpret_cast<char *>(hash), SHA256_DIGEST_LENGTH);
    return hashBytes.toHex();
}

// M2：PBKDF2-HMAC-SHA256 慢哈希
QString EncryptionManager::hashPasswordWithSalt(const QString &password, int iterations)
{
    // 生成 16 字节随机盐
    unsigned char salt[16];
    RAND_bytes(salt, sizeof(salt));
    QByteArray saltBytes(reinterpret_cast<char *>(salt), sizeof(salt));

    // PBKDF2 派生 32 字节密钥
    unsigned char derived[32];
    const QByteArray pwdBytes = password.toUtf8();
    PKCS5_PBKDF2_HMAC(pwdBytes.constData(), pwdBytes.size(),
                        reinterpret_cast<const unsigned char *>(saltBytes.constData()),
                        saltBytes.size(),
                        iterations, EVP_sha256(),
                        sizeof(derived), derived);

    QByteArray hashBytes(reinterpret_cast<char *>(derived), sizeof(derived));

    // 格式: "v1:<iterations>:<salt-hex>:<hash-hex>"
    return QString("v1:%1:%2:%3")
        .arg(iterations)
        .arg(QString::fromLatin1(saltBytes.toHex()))
        .arg(QString::fromLatin1(hashBytes.toHex()));
}

bool EncryptionManager::verifyPassword(const QString &password, const QString &storedHash)
{
    // 解析 "v1:<iterations>:<salt-hex>:<hash-hex>"
    const QStringList parts = storedHash.split(QLatin1Char(':'));
    if (parts.size() != 4 || parts[0] != "v1") {
        return false;
    }

    bool ok = false;
    const int iterations = parts[1].toInt(&ok);
    if (!ok || iterations <= 0) {
        return false;
    }

    const QByteArray saltBytes = QByteArray::fromHex(parts[2].toLatin1());
    const QByteArray expectedHash = QByteArray::fromHex(parts[3].toLatin1());

    if (saltBytes.isEmpty() || expectedHash.isEmpty()) {
        return false;
    }

    // 用相同盐 + 迭代次数重新派生
    unsigned char derived[32];
    const QByteArray pwdBytes = password.toUtf8();
    PKCS5_PBKDF2_HMAC(pwdBytes.constData(), pwdBytes.size(),
                        reinterpret_cast<const unsigned char *>(saltBytes.constData()),
                        saltBytes.size(),
                        iterations, EVP_sha256(),
                        sizeof(derived), derived);

    QByteArray derivedBytes(reinterpret_cast<char *>(derived), sizeof(derived));

    // 常数时间比较
    if (derivedBytes.size() != expectedHash.size()) {
        return false;
    }
    volatile unsigned char diff = 0;
    for (int i = 0; i < derivedBytes.size(); ++i) {
        diff |= static_cast<unsigned char>(derivedBytes[i]) ^ static_cast<unsigned char>(expectedHash[i]);
    }
    return diff == 0;
}

// M2：Token 工具
QString EncryptionManager::generateToken(int byteLength)
{
    QByteArray buf(byteLength, Qt::Uninitialized);
    RAND_bytes(reinterpret_cast<unsigned char *>(buf.data()), byteLength);
    return QString::fromLatin1(buf.toHex());
}

QString EncryptionManager::hashToken(const QString &token)
{
    return encryptPassword(token); // SHA-256 足够用于 token 存储
}
