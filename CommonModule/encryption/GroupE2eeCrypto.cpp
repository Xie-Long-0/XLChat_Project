#include "GroupE2eeCrypto.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "security/SecureMemory.h"

namespace XYChat::Security
{

namespace
{
constexpr int Ed25519KeySize = 32;
constexpr int Ed25519SigSize = 64;
constexpr int ChainKeySize = 32;
constexpr int GcmIvSize = 12;
constexpr int GcmTagSize = 16;
constexpr int DistributionVersion = 1;
constexpr int GroupMessageVersion = 1;
const QByteArray ChainHkdfSalt = QByteArrayLiteral("xychat-grp-chain");
const QByteArray MessageHkdfInfo = QByteArrayLiteral("xychat-grp-msg");

QString toBase64(const QByteArray &data)
{
    return QString::fromLatin1(data.toBase64());
}

QByteArray fromBase64(const QString &data, bool *ok)
{
    const QByteArray decoded = QByteArray::fromBase64(data.toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
    if (ok) {
        *ok = !decoded.isEmpty() || data.isEmpty();
    }
    return decoded;
}

QByteArray hkdfSingle(const QByteArray &key, const QByteArray &salt, const QByteArray &info)
{
    QByteArray derived;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx) {
        return derived;
    }
    size_t outLen = 32;
    derived.resize(32);
    bool ok = EVP_PKEY_derive_init(ctx) > 0
        && EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) > 0
        && EVP_PKEY_CTX_set1_hkdf_salt(ctx,
                                       reinterpret_cast<const unsigned char *>(salt.constData()),
                                       salt.size()) > 0
        && EVP_PKEY_CTX_set1_hkdf_key(ctx,
                                      reinterpret_cast<const unsigned char *>(key.constData()),
                                      key.size()) > 0
        && (!info.isEmpty()
            ? EVP_PKEY_CTX_add1_hkdf_info(ctx,
                                          reinterpret_cast<const unsigned char *>(info.constData()),
                                          info.size()) > 0
            : true)
        && EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char *>(derived.data()), &outLen) > 0
        && outLen == 32;

    EVP_PKEY_CTX_free(ctx);
    if (!ok) {
        SecureMemory::wipe(derived);
        return {};
    }
    return derived;
}

EVP_PKEY *edPrivateKey(const QByteArray &privateKey)
{
    if (privateKey.size() != Ed25519KeySize) {
        return nullptr;
    }
    return EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                        reinterpret_cast<const unsigned char *>(privateKey.constData()),
                                        Ed25519KeySize);
}

EVP_PKEY *edPublicKey(const QByteArray &publicKey)
{
    if (publicKey.size() != Ed25519KeySize) {
        return nullptr;
    }
    return EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                       reinterpret_cast<const unsigned char *>(publicKey.constData()),
                                       Ed25519KeySize);
}
} // namespace

GroupE2eeCrypto::SenderKey GroupE2eeCrypto::generateSenderKey()
{
    SenderKey key;

    EVP_PKEY *pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
    if (!pkey) {
        return key;
    }

    key.publicSigningKey.resize(Ed25519KeySize);
    key.privateSigningKey.resize(Ed25519KeySize);
    size_t pubLen = Ed25519KeySize;
    size_t privLen = Ed25519KeySize;
    bool ok = EVP_PKEY_get_raw_public_key(pkey,
                                          reinterpret_cast<unsigned char *>(key.publicSigningKey.data()),
                                          &pubLen) > 0
        && EVP_PKEY_get_raw_private_key(pkey,
                                        reinterpret_cast<unsigned char *>(key.privateSigningKey.data()),
                                        &privLen) > 0;
    EVP_PKEY_free(pkey);

    if (!ok || pubLen != Ed25519KeySize || privLen != Ed25519KeySize) {
        SecureMemory::wipe(key.privateSigningKey);
        return SenderKey{};
    }

    key.chainKey = E2eeCrypto::generateRandomBytes(ChainKeySize);
    if (key.chainKey.size() != ChainKeySize) {
        SecureMemory::wipe(key.privateSigningKey);
        SecureMemory::wipe(key.chainKey);
        return SenderKey{};
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(key.publicSigningKey.constData()),
           key.publicSigningKey.size(), hash);
    key.keyId = QString::fromLatin1(QByteArray(reinterpret_cast<char *>(hash), 16).toHex());
    key.iteration = 0;
    key.valid = true;
    return key;
}

QByteArray GroupE2eeCrypto::ratchetChainKey(const QByteArray &chainKey)
{
    if (chainKey.size() != ChainKeySize) {
        return {};
    }
    return hkdfSingle(chainKey, ChainHkdfSalt, QByteArray());
}

QByteArray GroupE2eeCrypto::deriveMessageKey(const QByteArray &chainKey, int iteration)
{
    if (chainKey.size() != ChainKeySize || iteration < 0) {
        return {};
    }
    const QByteArray info = MessageHkdfInfo + QByteArray::number(iteration);
    return hkdfSingle(chainKey, ChainHkdfSalt, info);
}

QByteArray GroupE2eeCrypto::sign(const QByteArray &privateSigningKey,
                                 const QByteArray &message)
{
    if (privateSigningKey.size() != Ed25519KeySize || message.isEmpty()) {
        return {};
    }

    EVP_PKEY *pkey = edPrivateKey(privateSigningKey);
    if (!pkey) {
        return {};
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    QByteArray signature;
    bool ok = ctx != nullptr
        && EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey) > 0;

    size_t sigLen = Ed25519SigSize;
    signature.resize(Ed25519SigSize);
    ok = ok
        && EVP_DigestSign(ctx,
                          reinterpret_cast<unsigned char *>(signature.data()),
                          &sigLen,
                          reinterpret_cast<const unsigned char *>(message.constData()),
                          message.size()) > 0
        && sigLen == Ed25519SigSize;

    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    if (!ok) {
        return {};
    }
    return signature;
}

bool GroupE2eeCrypto::verify(const QByteArray &publicSigningKey,
                             const QByteArray &message,
                             const QByteArray &signature)
{
    if (publicSigningKey.size() != Ed25519KeySize
        || signature.size() != Ed25519SigSize
        || message.isEmpty()) {
        return false;
    }

    EVP_PKEY *pkey = edPublicKey(publicSigningKey);
    if (!pkey) {
        return false;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = ctx != nullptr
        && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) > 0
        && EVP_DigestVerify(ctx,
                            reinterpret_cast<const unsigned char *>(signature.constData()),
                            signature.size(),
                            reinterpret_cast<const unsigned char *>(message.constData()),
                            message.size()) > 0;

    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

GroupE2eeCrypto::EncryptedMessage GroupE2eeCrypto::encryptMessage(SenderKey &key,
                                                                  const QByteArray &plaintext)
{
    EncryptedMessage result;
    if (!key.valid || plaintext.isEmpty()) {
        return result;
    }

    // ratchet 前进一步
    const QByteArray newChainKey = ratchetChainKey(key.chainKey);
    if (newChainKey.isEmpty()) {
        return result;
    }
    SecureMemory::wipe(key.chainKey);
    key.chainKey = newChainKey;
    key.iteration += 1;

    QByteArray messageKey = deriveMessageKey(key.chainKey, key.iteration);
    if (messageKey.isEmpty()) {
        return result;
    }

    const auto gcm = E2eeCrypto::aesGcmEncrypt(messageKey, plaintext);
    SecureMemory::wipe(messageKey);
    if (!gcm.valid) {
        return result;
    }

    const QByteArray signedPayload = gcm.iv + gcm.ciphertext;
    const QByteArray signature = sign(key.privateSigningKey, signedPayload);
    if (signature.isEmpty()) {
        return result;
    }

    result.keyId = key.keyId;
    result.iteration = key.iteration;
    result.iv = gcm.iv;
    result.ciphertext = gcm.ciphertext;
    result.signature = signature;
    result.valid = true;
    return result;
}

QByteArray GroupE2eeCrypto::decryptMessage(QByteArray &chainKey,
                                           int &iteration,
                                           const QByteArray &publicSigningKey,
                                           const EncryptedMessage &msg)
{
    if (!msg.valid
        || msg.iv.size() != GcmIvSize
        || msg.ciphertext.size() < GcmTagSize
        || msg.signature.size() != Ed25519SigSize
        || msg.iteration <= 0
        || chainKey.size() != ChainKeySize
        || iteration < 0) {
        return {};
    }

    // 不允许回退（重放/乱序）
    if (msg.iteration <= iteration) {
        return {};
    }

    // DoS 防护：拒绝超大跳跃（msg.iteration 由对端控制，上限内才允许同步 ratchet）
    if (msg.iteration - iteration > MaxRatchetSteps) {
        return {};
    }

    // 将本地 chainKey ratchet 到消息迭代次数
    QByteArray currentChainKey = chainKey;
    int currentIteration = iteration;
    while (currentIteration < msg.iteration) {
        const QByteArray next = ratchetChainKey(currentChainKey);
        if (next.isEmpty()) {
            SecureMemory::wipe(currentChainKey);
            return {};
        }
        currentChainKey = next;
        ++currentIteration;
    }

    QByteArray messageKey = deriveMessageKey(currentChainKey, currentIteration);
    if (messageKey.isEmpty()) {
        SecureMemory::wipe(currentChainKey);
        return {};
    }

    QByteArray plaintext = E2eeCrypto::aesGcmDecrypt(messageKey, msg.iv, msg.ciphertext);
    SecureMemory::wipe(messageKey);
    if (plaintext.isEmpty()) {
        SecureMemory::wipe(currentChainKey);
        return {};
    }

    // 验证签名（签名在解密之后：伪造签名无法通过 GCM 认证）
    const QByteArray signedPayload = msg.iv + msg.ciphertext;
    if (!verify(publicSigningKey, signedPayload, msg.signature)) {
        SecureMemory::wipe(plaintext);
        SecureMemory::wipe(currentChainKey);
        return {};
    }

    // 更新持久化状态
    SecureMemory::wipe(chainKey);
    chainKey = currentChainKey;
    iteration = currentIteration;
    return plaintext;
}

QJsonObject GroupE2eeCrypto::encodeDistribution(qint64 groupId,
                                                qint64 senderUserId,
                                                const QString &senderDeviceId,
                                                const SenderKey &key,
                                                const QList<DistributionEntry> &entries)
{
    QJsonArray devices;
    for (const DistributionEntry &entry : entries) {
        QJsonObject obj;
        obj["userId"] = entry.userId;
        obj["deviceId"] = entry.deviceId;
        obj["prekeyId"] = entry.envelope.prekeyId;
        obj["eph"] = toBase64(entry.envelope.ephemeralPublicKey);
        obj["iv"] = toBase64(entry.envelope.iv);
        obj["ct"] = toBase64(entry.envelope.ciphertext);
        devices.append(obj);
    }

    QJsonObject root;
    root["v"] = DistributionVersion;
    root["type"] = QStringLiteral("sender_key_distribution");
    root["groupId"] = groupId;
    root["senderUserId"] = senderUserId;
    root["senderDeviceId"] = senderDeviceId;
    root["keyId"] = key.keyId;
    root["publicSigningKey"] = toBase64(key.publicSigningKey);
    root["devices"] = devices;
    return root;
}

bool GroupE2eeCrypto::decodeDistribution(const QString &content,
                                         qint64 &groupId,
                                         qint64 &senderUserId,
                                         QString &senderDeviceId,
                                         SenderKey &key,
                                         QList<DistributionEntry> &entries)
{
    groupId = 0;
    senderUserId = 0;
    senderDeviceId.clear();
    key = SenderKey{};
    entries.clear();

    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    if (root["v"].toInt(-1) != DistributionVersion
        || root["type"].toString() != QLatin1String("sender_key_distribution")) {
        return false;
    }

    groupId = root["groupId"].toVariant().toLongLong();
    senderUserId = root["senderUserId"].toVariant().toLongLong();
    senderDeviceId = root["senderDeviceId"].toString();
    key.keyId = root["keyId"].toString();
    key.publicSigningKey = fromBase64(root["publicSigningKey"].toString(), nullptr);
    key.valid = !key.keyId.isEmpty() && key.publicSigningKey.size() == Ed25519KeySize;

    const QJsonArray devices = root["devices"].toArray();
    for (const QJsonValue &value : devices) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        DistributionEntry entry;
        entry.userId = obj["userId"].toVariant().toLongLong();
        entry.deviceId = obj["deviceId"].toString();
        entry.envelope.deviceId = entry.deviceId;
        entry.envelope.prekeyId = static_cast<qint64>(obj["prekeyId"].toDouble());
        entry.envelope.ephemeralPublicKey = fromBase64(obj["eph"].toString(), nullptr);
        entry.envelope.iv = fromBase64(obj["iv"].toString(), nullptr);
        entry.envelope.ciphertext = fromBase64(obj["ct"].toString(), nullptr);
        if (entry.userId <= 0 || entry.deviceId.isEmpty() || entry.envelope.prekeyId < 0
            || entry.envelope.ephemeralPublicKey.size() != 32
            || entry.envelope.iv.size() != GcmIvSize
            || entry.envelope.ciphertext.size() < GcmTagSize) {
            continue;
        }
        entries.append(entry);
    }

    return key.valid;
}

bool GroupE2eeCrypto::looksLikeDistribution(const QString &content)
{
    if (content.isEmpty() || content[0] != QLatin1Char('{')) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    return root["v"].toInt(-1) == DistributionVersion
        && root["type"].toString() == QLatin1String("sender_key_distribution");
}

QJsonObject GroupE2eeCrypto::encodeGroupMessage(const EncryptedMessage &msg,
                                                const QString &senderDeviceId)
{
    QJsonObject root;
    root["v"] = GroupMessageVersion;
    root["type"] = QStringLiteral("group_e2ee");
    root["keyId"] = msg.keyId;
    root["iteration"] = msg.iteration;
    root["senderDeviceId"] = senderDeviceId;
    root["iv"] = toBase64(msg.iv);
    root["ct"] = toBase64(msg.ciphertext);
    root["sig"] = toBase64(msg.signature);
    return root;
}

bool GroupE2eeCrypto::decodeGroupMessage(const QString &content, EncryptedMessage &msg,
                                         QString *senderDeviceId)
{
    msg = EncryptedMessage{};
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    if (root["v"].toInt(-1) != GroupMessageVersion
        || root["type"].toString() != QLatin1String("group_e2ee")) {
        return false;
    }

    msg.keyId = root["keyId"].toString();
    msg.iteration = root["iteration"].toInt();
    if (senderDeviceId) {
        *senderDeviceId = root["senderDeviceId"].toString();
    }
    msg.iv = fromBase64(root["iv"].toString(), nullptr);
    msg.ciphertext = fromBase64(root["ct"].toString(), nullptr);
    msg.signature = fromBase64(root["sig"].toString(), nullptr);
    msg.valid = !msg.keyId.isEmpty()
        && msg.iteration > 0
        && msg.iteration <= MaxMessageIteration
        && msg.iv.size() == GcmIvSize
        && msg.ciphertext.size() >= GcmTagSize
        && msg.signature.size() == Ed25519SigSize;
    return msg.valid;
}

bool GroupE2eeCrypto::looksLikeGroupMessage(const QString &content)
{
    if (content.isEmpty() || content[0] != QLatin1Char('{')) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    return root["v"].toInt(-1) == GroupMessageVersion
        && root["type"].toString() == QLatin1String("group_e2ee");
}

} // namespace XYChat::Security
