#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "E2eeCrypto.h"

namespace XYChat::Security
{

/**
 * M7b: 群聊端到端加密（简化 Signal Sender Keys）
 *
 * - 每个发送方在每个群中独立生成 Sender Key：
 *   chainKey（32 字节随机种子） + Ed25519 签名密钥对。
 * - 群消息使用 chainKey ratchet 派生消息密钥，AES-256-GCM 加密，
 *   Ed25519 签名（iv || ciphertext）保证发送方身份可验证。
 * - chainKey 通过 pairwise E2EE（X25519 身份/预密钥）分发给每个群成员。
 * - 服务端只转发密文与分发消息，无法读取群消息正文。
 */
class GroupE2eeCrypto
{
public:
    // DoS 防护：单次解密允许的最大 ratchet 步数（参考 Signal skipped-key 上限），
    // 恶意成员发送超大 iteration 时直接拒绝，避免同步循环长时间占用线程
    static constexpr int MaxRatchetSteps = 2000;
    // 群消息 envelope 中 iteration 的绝对上界（解码侧纵深防御）
    static constexpr int MaxMessageIteration = 100000000;

    struct SenderKey
    {
        QString keyId;            // 唯一标识：SHA-256(publicSigningKey).hex 前 32 字符
        QByteArray chainKey;      // 32 字节链式密钥种子（保密）
        QByteArray publicSigningKey;  // 32 字节 Ed25519 公钥
        QByteArray privateSigningKey; // 32 字节 Ed25519 私钥
        int iteration = 0;        // 当前已使用迭代次数
        bool valid = false;
    };

    // 生成新的 Sender Key
    static SenderKey generateSenderKey();

    // chainKey 前向 ratchet：HKDF-SHA256(chainKey, salt="xychat-grp-chain")
    static QByteArray ratchetChainKey(const QByteArray &chainKey);

    // 由 chainKey 与迭代次数派生 32 字节消息密钥
    static QByteArray deriveMessageKey(const QByteArray &chainKey, int iteration);

    // Ed25519 签名 / 验证（原始 32 字节公私钥）
    static QByteArray sign(const QByteArray &privateSigningKey,
                           const QByteArray &message);
    static bool verify(const QByteArray &publicSigningKey,
                       const QByteArray &message,
                       const QByteArray &signature);

    // 单条群消息加密结果
    struct EncryptedMessage
    {
        QString keyId;
        int iteration = 0;
        QByteArray iv;
        QByteArray ciphertext; // 密文 + GCM tag
        QByteArray signature;
        bool valid = false;
    };

    // 加密一条群消息（会修改 key.chainKey / key.iteration）
    static EncryptedMessage encryptMessage(SenderKey &key,
                                           const QByteArray &plaintext);

    // 解密群消息（chainKey 为接收方保存的链式密钥，publicSigningKey 为发送方公钥）
    // 成功返回明文；失败返回空；iteration 跳跃超过 MaxRatchetSteps 时拒绝（DoS 防护）
    static QByteArray decryptMessage(QByteArray &chainKey,
                                     int &iteration,
                                     const QByteArray &publicSigningKey,
                                     const EncryptedMessage &msg);

    // 分发消息：将 chainKey 用 pairwise E2EE 加密后发给每个目标设备
    struct DistributionEntry
    {
        qint64 userId = 0;
        QString deviceId;
        E2eeCrypto::EnvelopeEntry envelope; // 加密后的 chainKey
    };

    static QJsonObject encodeDistribution(qint64 groupId,
                                          qint64 senderUserId,
                                          const QString &senderDeviceId,
                                          const SenderKey &key,
                                          const QList<DistributionEntry> &entries);

    static bool decodeDistribution(const QString &content,
                                   qint64 &groupId,
                                   qint64 &senderUserId,
                                   QString &senderDeviceId,
                                   SenderKey &key, // 仅填充 public 字段
                                   QList<DistributionEntry> &entries);

    static bool looksLikeDistribution(const QString &content);

    // 群消息 envelope 编解码（区别于 pairwise envelope）
    static QJsonObject encodeGroupMessage(const EncryptedMessage &msg,
                                          const QString &senderDeviceId = {});
    static bool decodeGroupMessage(const QString &content, EncryptedMessage &msg,
                                   QString *senderDeviceId = nullptr);
    static bool looksLikeGroupMessage(const QString &content);
};

} // namespace XYChat::Security
