/**
 * M7b: 群聊 Sender-Key E2EE 原语单元测试
 *
 * 覆盖：SenderKey 生成、chain-key ratchet、消息加解密、签名验证、
 *       分发消息编解码、群消息 envelope 编解码，
 *       以及 DoS 防护（ratchet 跳跃上限、iteration 上界）与
 *       服务端 fail-closed envelope 校验的判定函数。
 */
#include <QtTest>
#include <QJsonDocument>

#include <climits>

#include "encryption/GroupE2eeCrypto.h"

using namespace XYChat::Security;

class TestGroupE2eeCrypto : public QObject
{
    Q_OBJECT

private slots:
    void generateSenderKeyProducesValidKey()
    {
        const auto key = GroupE2eeCrypto::generateSenderKey();
        QVERIFY(key.valid);
        QCOMPARE(key.chainKey.size(), 32);
        QCOMPARE(key.publicSigningKey.size(), 32);
        QCOMPARE(key.privateSigningKey.size(), 32);
        QVERIFY(!key.keyId.isEmpty());
        QCOMPARE(key.iteration, 0);
    }

    void generatedKeysAreUnique()
    {
        const auto a = GroupE2eeCrypto::generateSenderKey();
        const auto b = GroupE2eeCrypto::generateSenderKey();
        QVERIFY(a.valid && b.valid);
        QVERIFY(a.keyId != b.keyId);
        QVERIFY(a.publicSigningKey != b.publicSigningKey);
        QVERIFY(a.chainKey != b.chainKey);
    }

    void ratchetChainKeyIsDeterministicAndDifferent()
    {
        const auto key = GroupE2eeCrypto::generateSenderKey();
        const auto next1 = GroupE2eeCrypto::ratchetChainKey(key.chainKey);
        const auto next2 = GroupE2eeCrypto::ratchetChainKey(key.chainKey);
        QCOMPARE(next1.size(), 32);
        QCOMPARE(next1, next2);
        QVERIFY(next1 != key.chainKey);
    }

    void deriveMessageKeyDependsOnIteration()
    {
        const auto key = GroupE2eeCrypto::generateSenderKey();
        const auto mk0 = GroupE2eeCrypto::deriveMessageKey(key.chainKey, 0);
        const auto mk1 = GroupE2eeCrypto::deriveMessageKey(key.chainKey, 1);
        QCOMPARE(mk0.size(), 32);
        QCOMPARE(mk1.size(), 32);
        QVERIFY(mk0 != mk1);
    }

    void encryptThenDecryptRoundTrip()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        const QByteArray initialChainKey = key.chainKey;
        const int initialIteration = key.iteration;
        const QByteArray plaintext = "Hello, group E2EE!";

        const auto encrypted = GroupE2eeCrypto::encryptMessage(key, plaintext);
        QVERIFY(encrypted.valid);
        QCOMPARE(encrypted.keyId, key.keyId);
        QCOMPARE(encrypted.iteration, 1);
        QCOMPARE(encrypted.iv.size(), 12);
        QVERIFY(encrypted.ciphertext.size() >= 16);
        QCOMPARE(encrypted.signature.size(), 64);
        // 加密后 chainKey 已 ratchet，iteration 变为 1
        QCOMPARE(key.iteration, 1);

        // decryptMessage 期望传入发送前状态（chainKey_0, iteration=0），
        // 内部 ratchet 到 msg.iteration 后解密；解密后状态与发送后一致
        QByteArray chainKey = initialChainKey;
        int iteration = initialIteration;
        const QByteArray decrypted = GroupE2eeCrypto::decryptMessage(
            chainKey, iteration, key.publicSigningKey, encrypted);
        QCOMPARE(decrypted, plaintext);
        QCOMPARE(iteration, 1);
        QCOMPARE(chainKey, key.chainKey);
    }

    void decryptRejectsIterationRollback()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        const QByteArray initialChainKey = key.chainKey;
        const int initialIteration = key.iteration;
        const QByteArray plaintext = "message one";
        const auto encrypted = GroupE2eeCrypto::encryptMessage(key, plaintext);

        // 解密时传入发送前状态（iteration=0），内部 ratchet 到 1
        QByteArray chainKey = initialChainKey;
        int iteration = initialIteration;
        QVERIFY(!GroupE2eeCrypto::decryptMessage(
            chainKey, iteration, key.publicSigningKey, encrypted).isEmpty());
        QCOMPARE(iteration, 1);

        // 用已处理到 iteration=1 的状态再次解密同一条消息：不允许回退
        QVERIFY(GroupE2eeCrypto::decryptMessage(
            chainKey, iteration, key.publicSigningKey, encrypted).isEmpty());
    }

    void decryptRejectsTamperedCiphertext()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        const QByteArray initialChainKey = key.chainKey;
        const QByteArray plaintext = "tamper test";
        auto encrypted = GroupE2eeCrypto::encryptMessage(key, plaintext);
        QVERIFY(encrypted.valid);

        encrypted.ciphertext[0] ^= 0xFF;
        QByteArray chainKey = initialChainKey;
        int iteration = 0;
        QVERIFY(GroupE2eeCrypto::decryptMessage(
            chainKey, iteration, key.publicSigningKey, encrypted).isEmpty());
    }

    void decryptRejectsTamperedSignature()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        const QByteArray initialChainKey = key.chainKey;
        const QByteArray plaintext = "sig tamper test";
        auto encrypted = GroupE2eeCrypto::encryptMessage(key, plaintext);
        QVERIFY(encrypted.valid);

        encrypted.signature[0] ^= 0xFF;
        QByteArray chainKey = initialChainKey;
        int iteration = 0;
        QVERIFY(GroupE2eeCrypto::decryptMessage(
            chainKey, iteration, key.publicSigningKey, encrypted).isEmpty());
    }

    void decryptRejectsWrongPublicSigningKey()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        const QByteArray initialChainKey = key.chainKey;
        const auto other = GroupE2eeCrypto::generateSenderKey();
        const QByteArray plaintext = "wrong signer";
        const auto encrypted = GroupE2eeCrypto::encryptMessage(key, plaintext);

        QByteArray chainKey = initialChainKey;
        int iteration = 0;
        QVERIFY(GroupE2eeCrypto::decryptMessage(
            chainKey, iteration, other.publicSigningKey, encrypted).isEmpty());
    }

    void distributionEncodeDecodeRoundTrip()
    {
        const auto key = GroupE2eeCrypto::generateSenderKey();
        GroupE2eeCrypto::DistributionEntry entry;
        entry.userId = 42;
        entry.deviceId = "deviceA";
        entry.envelope.prekeyId = 7;
        entry.envelope.ephemeralPublicKey = QByteArray(32, 'E');
        entry.envelope.iv = QByteArray(12, 'I');
        entry.envelope.ciphertext = QByteArray(16, 'C');

        const QJsonObject encoded = GroupE2eeCrypto::encodeDistribution(
            100, 1, "localDevice", key, {entry});
        QCOMPARE(encoded["type"].toString(), QString("sender_key_distribution"));
        QCOMPARE(encoded["groupId"].toVariant().toLongLong(), 100LL);

        qint64 groupId = 0;
        qint64 senderUserId = 0;
        QString senderDeviceId;
        GroupE2eeCrypto::SenderKey decodedKey;
        QList<GroupE2eeCrypto::DistributionEntry> decodedEntries;
        QVERIFY(GroupE2eeCrypto::decodeDistribution(
            QJsonDocument(encoded).toJson(QJsonDocument::Compact),
            groupId, senderUserId, senderDeviceId, decodedKey, decodedEntries));
        QCOMPARE(groupId, 100LL);
        QCOMPARE(senderUserId, 1LL);
        QCOMPARE(senderDeviceId, QString("localDevice"));
        QCOMPARE(decodedKey.keyId, key.keyId);
        QCOMPARE(decodedKey.publicSigningKey, key.publicSigningKey);
        QCOMPARE(decodedEntries.size(), 1);
        QCOMPARE(decodedEntries.at(0).userId, 42LL);
        QCOMPARE(decodedEntries.at(0).deviceId, QString("deviceA"));
    }

    void groupMessageEncodeDecodeRoundTrip()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        const QByteArray plaintext = "group envelope test";
        const auto encrypted = GroupE2eeCrypto::encryptMessage(key, plaintext);

        const QJsonObject encoded = GroupE2eeCrypto::encodeGroupMessage(
            encrypted, "senderDevice");
        const QString json = QJsonDocument(encoded).toJson(QJsonDocument::Compact);
        QVERIFY(GroupE2eeCrypto::looksLikeGroupMessage(json));
        QVERIFY(!GroupE2eeCrypto::looksLikeDistribution(json));

        GroupE2eeCrypto::EncryptedMessage decoded;
        QString deviceId;
        QVERIFY(GroupE2eeCrypto::decodeGroupMessage(json, decoded, &deviceId));
        QCOMPARE(deviceId, QString("senderDevice"));
        QCOMPARE(decoded.keyId, encrypted.keyId);
        QCOMPARE(decoded.iteration, encrypted.iteration);
        QCOMPARE(decoded.iv, encrypted.iv);
        QCOMPARE(decoded.ciphertext, encrypted.ciphertext);
        QCOMPARE(decoded.signature, encrypted.signature);
        QVERIFY(decoded.valid);
    }

    void looksLikeDetectsFormats()
    {
        QVERIFY(!GroupE2eeCrypto::looksLikeGroupMessage("{}"));
        QVERIFY(!GroupE2eeCrypto::looksLikeDistribution("{}"));

        const QJsonObject dist;
        QVERIFY(!GroupE2eeCrypto::looksLikeDistribution(
            QJsonDocument(dist).toJson(QJsonDocument::Compact)));

        auto key = GroupE2eeCrypto::generateSenderKey();
        const auto encrypted = GroupE2eeCrypto::encryptMessage(key, "x");
        QVERIFY(GroupE2eeCrypto::looksLikeGroupMessage(
            QJsonDocument(GroupE2eeCrypto::encodeGroupMessage(encrypted)).toJson(
                QJsonDocument::Compact)));
    }

    // DoS 防护：签名完全合法但 iteration 跳跃超过 MaxRatchetSteps 的消息必须被拒绝，
    // 且接收方状态不得变更（恶意远未来 iteration 不能消耗 ratchet 计算）
    void decryptRejectsIterationJumpBeyondLimit()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        const QByteArray initialChainKey = key.chainKey;

        // 发送方连续加密 MaxRatchetSteps+1 条，取最后一条（iteration=2001）
        const int target = GroupE2eeCrypto::MaxRatchetSteps + 1;
        GroupE2eeCrypto::EncryptedMessage last;
        for (int i = 0; i < target; ++i) {
            last = GroupE2eeCrypto::encryptMessage(key, QByteArray("msg ") + QByteArray::number(i));
            QVERIFY(last.valid);
        }
        QCOMPARE(last.iteration, target);

        QByteArray chainKey = initialChainKey;
        int iteration = 0;
        QVERIFY(GroupE2eeCrypto::decryptMessage(
            chainKey, iteration, key.publicSigningKey, last).isEmpty());
        // 拒绝时状态不变：chainKey 未被 ratchet，iteration 未推进
        QCOMPARE(iteration, 0);
        QCOMPARE(chainKey, initialChainKey);
    }

    // DoS 防护边界：跳跃恰好等于 MaxRatchetSteps 时仍可正常同步解密
    void decryptAcceptsIterationJumpAtLimit()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        const QByteArray initialChainKey = key.chainKey;

        const int target = GroupE2eeCrypto::MaxRatchetSteps;
        const QByteArray plaintext = "boundary message";
        GroupE2eeCrypto::EncryptedMessage last;
        for (int i = 0; i < target; ++i) {
            last = GroupE2eeCrypto::encryptMessage(key, (i == target - 1) ? plaintext
                                                                          : QByteArray("filler"));
            QVERIFY(last.valid);
        }
        QCOMPARE(last.iteration, target);

        QByteArray chainKey = initialChainKey;
        int iteration = 0;
        QCOMPARE(GroupE2eeCrypto::decryptMessage(
            chainKey, iteration, key.publicSigningKey, last), plaintext);
        QCOMPARE(iteration, target);
        QCOMPARE(chainKey, key.chainKey);
    }

    // DoS 防护：手工构造的极端 iteration（审查报告攻击向量 INT_MAX）在解密与解码两侧均被拒绝
    void decryptAndDecodeRejectAbsurdIteration()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        auto encrypted = GroupE2eeCrypto::encryptMessage(key, "payload");
        QVERIFY(encrypted.valid);

        encrypted.iteration = INT_MAX;
        QByteArray chainKey = key.chainKey;
        int iteration = 0;
        QVERIFY(GroupE2eeCrypto::decryptMessage(
            chainKey, iteration, key.publicSigningKey, encrypted).isEmpty());
        QCOMPARE(iteration, 0);

        // 解码侧上界：iteration 超过 MaxMessageIteration 的 envelope 直接判为非法
        const QJsonObject overCap = GroupE2eeCrypto::encodeGroupMessage(encrypted);
        GroupE2eeCrypto::EncryptedMessage decoded;
        QVERIFY(!GroupE2eeCrypto::decodeGroupMessage(
            QJsonDocument(overCap).toJson(QJsonDocument::Compact), decoded));
        QVERIFY(!decoded.valid);
    }

    // 解码侧 iteration 上界的边界值：MaxMessageIteration 本身合法，+1 拒绝
    void decodeGroupMessageIterationCapBoundary()
    {
        auto key = GroupE2eeCrypto::generateSenderKey();
        auto encrypted = GroupE2eeCrypto::encryptMessage(key, "cap boundary");

        QJsonObject atCap = GroupE2eeCrypto::encodeGroupMessage(encrypted);
        atCap["iteration"] = GroupE2eeCrypto::MaxMessageIteration;
        GroupE2eeCrypto::EncryptedMessage decoded;
        QVERIFY(GroupE2eeCrypto::decodeGroupMessage(
            QJsonDocument(atCap).toJson(QJsonDocument::Compact), decoded));
        QCOMPARE(decoded.iteration, GroupE2eeCrypto::MaxMessageIteration);

        QJsonObject overCap = atCap;
        overCap["iteration"] = GroupE2eeCrypto::MaxMessageIteration + 1;
        QVERIFY(!GroupE2eeCrypto::decodeGroupMessage(
            QJsonDocument(overCap).toJson(QJsonDocument::Compact), decoded));
    }

    // 服务端 fail-closed 门槛（RequestHandler::processSendGroupMessage 同源判定）：
    // e2ee_group 内容必须是合法群消息 envelope，明文/残缺结构一律拒绝
    void serverGateRejectsInvalidGroupEnvelope()
    {
        GroupE2eeCrypto::EncryptedMessage decoded;
        QString senderDeviceId;

        // 纯文本伪装成 e2ee_group 密文
        QVERIFY(!GroupE2eeCrypto::decodeGroupMessage("hello plaintext", decoded, &senderDeviceId));
        // JSON 但缺少关键字段
        QVERIFY(!GroupE2eeCrypto::decodeGroupMessage(
            "{\"v\":1,\"type\":\"group_e2ee\",\"keyId\":\"k\"}", decoded, &senderDeviceId));
        // 字段尺寸非法（iv 长度错误）
        auto key = GroupE2eeCrypto::generateSenderKey();
        auto encrypted = GroupE2eeCrypto::encryptMessage(key, "x");
        QJsonObject badIv = GroupE2eeCrypto::encodeGroupMessage(encrypted, "dev1");
        badIv["iv"] = QString::fromLatin1(QByteArray(8, 'I').toBase64());
        QVERIFY(!GroupE2eeCrypto::decodeGroupMessage(
            QJsonDocument(badIv).toJson(QJsonDocument::Compact), decoded, &senderDeviceId));
        // senderDeviceId 缺失时接收方无法定位 sender key，门槛同样拒绝
        QJsonObject noDevice = GroupE2eeCrypto::encodeGroupMessage(encrypted);
        QVERIFY(GroupE2eeCrypto::decodeGroupMessage(
            QJsonDocument(noDevice).toJson(QJsonDocument::Compact), decoded, &senderDeviceId));
        QVERIFY(senderDeviceId.isEmpty());
        // 合法 envelope 通过门槛
        QVERIFY(GroupE2eeCrypto::decodeGroupMessage(
            QJsonDocument(GroupE2eeCrypto::encodeGroupMessage(encrypted, "dev1"))
                .toJson(QJsonDocument::Compact), decoded, &senderDeviceId));
        QCOMPARE(senderDeviceId, QString("dev1"));
    }

    // 服务端 fail-closed 门槛：sender_key_distribution 必须可解码、含有效条目且 groupId 与会话一致
    void serverGateRejectsInvalidDistribution()
    {
        qint64 groupId = 0;
        qint64 senderUserId = 0;
        QString senderDeviceId;
        GroupE2eeCrypto::SenderKey key;
        QList<GroupE2eeCrypto::DistributionEntry> entries;

        // 明文伪装成分发消息
        QVERIFY(!GroupE2eeCrypto::decodeDistribution("plain text", groupId, senderUserId,
                                                     senderDeviceId, key, entries));
        // 结构合法但无任何设备条目：接收方无法获得 chain key，门槛拒绝
        const auto validKey = GroupE2eeCrypto::generateSenderKey();
        const QString noDevices = QJsonDocument(GroupE2eeCrypto::encodeDistribution(
            100, 1, "devA", validKey, {})).toJson(QJsonDocument::Compact);
        QVERIFY(GroupE2eeCrypto::decodeDistribution(noDevices, groupId, senderUserId,
                                                    senderDeviceId, key, entries));
        QVERIFY(entries.isEmpty());
        // groupId 与目标会话不一致：门槛拒绝（防止向其他群投递分发消息）
        GroupE2eeCrypto::DistributionEntry entry;
        entry.userId = 42;
        entry.deviceId = "devB";
        entry.envelope.prekeyId = 7;
        entry.envelope.ephemeralPublicKey = QByteArray(32, 'E');
        entry.envelope.iv = QByteArray(12, 'I');
        entry.envelope.ciphertext = QByteArray(16, 'C');
        const QString wrongGroup = QJsonDocument(GroupE2eeCrypto::encodeDistribution(
            999, 1, "devA", validKey, {entry})).toJson(QJsonDocument::Compact);
        QVERIFY(GroupE2eeCrypto::decodeDistribution(wrongGroup, groupId, senderUserId,
                                                    senderDeviceId, key, entries));
        QCOMPARE(groupId, 999LL);
        QVERIFY(groupId != 100LL);
        // 合法分发消息通过门槛
        const QString valid = QJsonDocument(GroupE2eeCrypto::encodeDistribution(
            100, 1, "devA", validKey, {entry})).toJson(QJsonDocument::Compact);
        QVERIFY(GroupE2eeCrypto::decodeDistribution(valid, groupId, senderUserId,
                                                    senderDeviceId, key, entries));
        QCOMPARE(groupId, 100LL);
        QCOMPARE(entries.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestGroupE2eeCrypto)
#include "TestGroupE2eeCrypto.moc"
