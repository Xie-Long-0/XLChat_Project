#include <QtTest/QtTest>
#include <QRegularExpression>
#include <QJsonDocument>

#include "EncryptionManager.h"
#include "E2eeCrypto.h"

using XYChat::Security::E2eeCrypto;

class TestEncryptionManager : public QObject
{
    Q_OBJECT

private slots:
    // M1 兼容测试
    void sha256DigestIsStable();
    void sha256DigestLengthIsHexEncoded();

    // M2 PBKDF2 测试
    void pbkdf2HashProducesValidFormat();
    void pbkdf2VerifyCorrectPassword();
    void pbkdf2RejectWrongPassword();
    void pbkdf2DifferentSaltsProduceDifferentHashes();
    void pbkdf2RejectInvalidStoredHash();

    // M2 Token 测试
    void generateTokenReturnsHex();
    void generateTokenIsUnique();
    void hashTokenIsDeterministic();

    // M6 E2EE 原语测试
    void x25519KeyPairIsValid();
    void x25519KeyPairFromPrivateKeyRestoresPublic();
    void ecdhSharedSecretMatchesBothDirections();
    void ecdhRejectsInvalidKeys();
    void hkdfDerivationIsDeterministic();
    void hkdfDifferentSecretsYieldDifferentKeys();
    void aesGcmRoundTrip();
    void aesGcmDetectsCiphertextTampering();
    void aesGcmDetectsIvTampering();
    void aesGcmFailsWithWrongKey();
    void fingerprintIsStable();
    void envelopeEncodeDecodeRoundTrip();
    void envelopeDecodeRejectsInvalid();
    void selfCopyEnvelopeRoundTrip();
    void fullE2eeMessageFlow();
};

// ── M1 兼容 ──
void TestEncryptionManager::sha256DigestIsStable()
{
    QCOMPARE(EncryptionManager::encryptPassword("passwd"),
             "0d6be69b264717f2dd33652e212b173104b4a647b7c11ae72e9885f11cd312fb");
}

void TestEncryptionManager::sha256DigestLengthIsHexEncoded()
{
    const QString digest = EncryptionManager::encryptPassword("any-password");
    QCOMPARE(digest.size(), 64);
    QVERIFY(digest.contains(QRegularExpression("^[0-9a-f]{64}$")));
}

// ── PBKDF2 ──
void TestEncryptionManager::pbkdf2HashProducesValidFormat()
{
    const QString hash = EncryptionManager::hashPasswordWithSalt("test-password");
    // 格式: "v1:<iterations>:<salt-hex>:<hash-hex>"
    const QStringList parts = hash.split(QLatin1Char(':'));
    QCOMPARE(parts.size(), 4);
    QCOMPARE(parts[0], "v1");
    QCOMPARE(parts[1].toInt(), EncryptionManager::defaultIterations());
    // salt: 16 bytes = 32 hex chars
    QCOMPARE(parts[2].size(), 32);
    // hash: 32 bytes = 64 hex chars
    QCOMPARE(parts[3].size(), 64);
}

void TestEncryptionManager::pbkdf2VerifyCorrectPassword()
{
    const QString hash = EncryptionManager::hashPasswordWithSalt("my-password");
    QVERIFY(EncryptionManager::verifyPassword("my-password", hash));
}

void TestEncryptionManager::pbkdf2RejectWrongPassword()
{
    const QString hash = EncryptionManager::hashPasswordWithSalt("correct-password");
    QVERIFY(!EncryptionManager::verifyPassword("wrong-password", hash));
}

void TestEncryptionManager::pbkdf2DifferentSaltsProduceDifferentHashes()
{
    const QString hash1 = EncryptionManager::hashPasswordWithSalt("same-password");
    const QString hash2 = EncryptionManager::hashPasswordWithSalt("same-password");
    // 由于随机盐，两次哈希结果不同
    QVERIFY(hash1 != hash2);
    // 但都能验证成功
    QVERIFY(EncryptionManager::verifyPassword("same-password", hash1));
    QVERIFY(EncryptionManager::verifyPassword("same-password", hash2));
}

void TestEncryptionManager::pbkdf2RejectInvalidStoredHash()
{
    QVERIFY(!EncryptionManager::verifyPassword("password", "invalid"));
    QVERIFY(!EncryptionManager::verifyPassword("password", "v2:100:aa:bb"));
    QVERIFY(!EncryptionManager::verifyPassword("password", "v1:0:aa:bb"));
}

// ── Token ──
void TestEncryptionManager::generateTokenReturnsHex()
{
    const QString token = EncryptionManager::generateToken();
    QCOMPARE(token.size(), 64); // 32 bytes = 64 hex chars
    QVERIFY(token.contains(QRegularExpression("^[0-9a-f]{64}$")));
}

void TestEncryptionManager::generateTokenIsUnique()
{
    const QString token1 = EncryptionManager::generateToken();
    const QString token2 = EncryptionManager::generateToken();
    QVERIFY(token1 != token2);
}

void TestEncryptionManager::hashTokenIsDeterministic()
{
    const QString token = EncryptionManager::generateToken();
    const QString hash1 = EncryptionManager::hashToken(token);
    const QString hash2 = EncryptionManager::hashToken(token);
    QCOMPARE(hash1, hash2);
}

// ── M6: E2EE 原语 ──
void TestEncryptionManager::x25519KeyPairIsValid()
{
    const auto kp = E2eeCrypto::generateX25519KeyPair();
    QVERIFY(kp.valid);
    QCOMPARE(kp.publicKey.size(), 32);
    QCOMPARE(kp.privateKey.size(), 32);

    const auto kp2 = E2eeCrypto::generateX25519KeyPair();
    QVERIFY(kp2.valid);
    QVERIFY(kp.publicKey != kp2.publicKey);
}

void TestEncryptionManager::x25519KeyPairFromPrivateKeyRestoresPublic()
{
    const auto kp = E2eeCrypto::generateX25519KeyPair();
    const auto restored = E2eeCrypto::keyPairFromPrivateKey(kp.privateKey);
    QVERIFY(restored.valid);
    QCOMPARE(restored.publicKey, kp.publicKey);

    // 非法私钥长度被拒绝
    QVERIFY(!E2eeCrypto::keyPairFromPrivateKey(QByteArray(16, 'x')).valid);
}

void TestEncryptionManager::ecdhSharedSecretMatchesBothDirections()
{
    const auto alice = E2eeCrypto::generateX25519KeyPair();
    const auto bob = E2eeCrypto::generateX25519KeyPair();

    const QByteArray s1 = E2eeCrypto::ecdh(alice.privateKey, bob.publicKey);
    const QByteArray s2 = E2eeCrypto::ecdh(bob.privateKey, alice.publicKey);
    QCOMPARE(s1.size(), 32);
    QCOMPARE(s1, s2);
}

void TestEncryptionManager::ecdhRejectsInvalidKeys()
{
    const auto alice = E2eeCrypto::generateX25519KeyPair();
    QVERIFY(E2eeCrypto::ecdh(alice.privateKey, QByteArray(31, 'x')).isEmpty());
    QVERIFY(E2eeCrypto::ecdh(QByteArray(), alice.publicKey).isEmpty());
}

void TestEncryptionManager::hkdfDerivationIsDeterministic()
{
    const QByteArray secret(64, 'k');
    QCOMPARE(E2eeCrypto::deriveMessageKey(secret), E2eeCrypto::deriveMessageKey(secret));
    QCOMPARE(E2eeCrypto::deriveMessageKey(secret).size(), 32);
}

void TestEncryptionManager::hkdfDifferentSecretsYieldDifferentKeys()
{
    const QByteArray s1(64, 'a');
    const QByteArray s2(64, 'b');
    QVERIFY(E2eeCrypto::deriveMessageKey(s1) != E2eeCrypto::deriveMessageKey(s2));
    QVERIFY(E2eeCrypto::deriveMessageKey(QByteArray()).isEmpty());
}

void TestEncryptionManager::aesGcmRoundTrip()
{
    const QByteArray key = E2eeCrypto::deriveMessageKey(QByteArray(64, 'k'));
    const QByteArray plaintext = QStringLiteral("你好，XYChat！").toUtf8();

    const auto gcm = E2eeCrypto::aesGcmEncrypt(key, plaintext);
    QVERIFY(gcm.valid);
    QCOMPARE(gcm.iv.size(), 12);
    QVERIFY(gcm.ciphertext != plaintext);

    QCOMPARE(E2eeCrypto::aesGcmDecrypt(key, gcm.iv, gcm.ciphertext), plaintext);
}

void TestEncryptionManager::aesGcmDetectsCiphertextTampering()
{
    const QByteArray key = E2eeCrypto::deriveMessageKey(QByteArray(64, 'k'));
    auto gcm = E2eeCrypto::aesGcmEncrypt(key, QByteArray("hello world"));
    QVERIFY(gcm.valid);

    gcm.ciphertext[0] = static_cast<char>(gcm.ciphertext[0]) ^ 0x01;
    QVERIFY(E2eeCrypto::aesGcmDecrypt(key, gcm.iv, gcm.ciphertext).isEmpty());
}

void TestEncryptionManager::aesGcmDetectsIvTampering()
{
    const QByteArray key = E2eeCrypto::deriveMessageKey(QByteArray(64, 'k'));
    auto gcm = E2eeCrypto::aesGcmEncrypt(key, QByteArray("hello world"));
    QVERIFY(gcm.valid);

    gcm.iv[0] = static_cast<char>(gcm.iv[0]) ^ 0x01;
    QVERIFY(E2eeCrypto::aesGcmDecrypt(key, gcm.iv, gcm.ciphertext).isEmpty());
}

void TestEncryptionManager::aesGcmFailsWithWrongKey()
{
    const QByteArray key1 = E2eeCrypto::deriveMessageKey(QByteArray(64, 'k'));
    const QByteArray key2 = E2eeCrypto::deriveMessageKey(QByteArray(64, 'j'));
    const auto gcm = E2eeCrypto::aesGcmEncrypt(key1, QByteArray("secret"));
    QVERIFY(gcm.valid);
    QVERIFY(E2eeCrypto::aesGcmDecrypt(key2, gcm.iv, gcm.ciphertext).isEmpty());
}

void TestEncryptionManager::fingerprintIsStable()
{
    const auto kp = E2eeCrypto::generateX25519KeyPair();
    QCOMPARE(E2eeCrypto::publicKeyFingerprint(kp.publicKey),
             E2eeCrypto::publicKeyFingerprint(kp.publicKey));
    QCOMPARE(E2eeCrypto::publicKeyFingerprint(kp.publicKey).size(), 32); // 16B hex
}

void TestEncryptionManager::envelopeEncodeDecodeRoundTrip()
{
    E2eeCrypto::EnvelopeEntry entry;
    entry.deviceId = "device-a";
    entry.prekeyId = 42;
    entry.ephemeralPublicKey = QByteArray(32, 'e');
    entry.iv = QByteArray(12, 'i');
    entry.ciphertext = QByteArray(48, 'c'); // 含 16B 标签

    const QJsonObject envelope = E2eeCrypto::encodeEnvelope({entry});
    const QString content = QString::fromUtf8(
        QJsonDocument(envelope).toJson(QJsonDocument::Compact));

    QVERIFY(E2eeCrypto::looksLikeEnvelope(content));

    bool ok = false;
    const auto decoded = E2eeCrypto::decodeEnvelope(content, &ok);
    QVERIFY(ok);
    QCOMPARE(decoded.size(), 1);
    QCOMPARE(decoded[0].deviceId, entry.deviceId);
    QCOMPARE(decoded[0].prekeyId, entry.prekeyId);
    QCOMPARE(decoded[0].ephemeralPublicKey, entry.ephemeralPublicKey);
    QCOMPARE(decoded[0].iv, entry.iv);
    QCOMPARE(decoded[0].ciphertext, entry.ciphertext);
}

void TestEncryptionManager::envelopeDecodeRejectsInvalid()
{
    bool ok = true;
    QVERIFY(E2eeCrypto::decodeEnvelope("not json", &ok).isEmpty() && !ok);
    QVERIFY(E2eeCrypto::decodeEnvelope("{\"v\":2}", &ok).isEmpty() && !ok);
    QVERIFY(E2eeCrypto::decodeEnvelope("{\"v\":1,\"devices\":[]}", &ok).isEmpty() && !ok);
    QVERIFY(!E2eeCrypto::looksLikeEnvelope("plain text message"));
    QVERIFY(!E2eeCrypto::looksLikeEnvelope("{\"v\":9,\"devices\":[]}"));
}

// 发送方自身拷贝（prekeyId=0，仅身份密钥）：重新登录后可解密自己发出的消息
void TestEncryptionManager::selfCopyEnvelopeRoundTrip()
{
    const auto identity = E2eeCrypto::generateX25519KeyPair();
    const auto eph = E2eeCrypto::generateX25519KeyPair();

    // 加密：shared = ECDH(eph, identity) 拼接两次凑足 64 字节
    const QByteArray dh = E2eeCrypto::ecdh(eph.privateKey, identity.publicKey);
    QCOMPARE(dh.size(), 32);
    const QByteArray key = E2eeCrypto::deriveMessageKey(dh + dh);
    const QString message = QStringLiteral("self copy message");
    const auto gcm = E2eeCrypto::aesGcmEncrypt(key, message.toUtf8());
    QVERIFY(gcm.valid);

    E2eeCrypto::EnvelopeEntry entry;
    entry.deviceId = "dev-self";
    entry.prekeyId = E2eeCrypto::SelfCopyPrekeyId;
    entry.ephemeralPublicKey = eph.publicKey;
    entry.iv = gcm.iv;
    entry.ciphertext = gcm.ciphertext;

    const QString content = QString::fromUtf8(
        QJsonDocument(E2eeCrypto::encodeEnvelope({entry})).toJson(QJsonDocument::Compact));

    // prekeyId=0 的条目能被正常解析（修复前被当作非法拒绝）
    bool ok = false;
    const auto decoded = E2eeCrypto::decodeEnvelope(content, &ok);
    QVERIFY(ok);
    QCOMPARE(decoded.size(), 1);
    QCOMPARE(decoded[0].prekeyId, E2eeCrypto::SelfCopyPrekeyId);

    // 解密：仅用身份私钥，不依赖任何预密钥
    const QByteArray dh2 = E2eeCrypto::ecdh(identity.privateKey, decoded[0].ephemeralPublicKey);
    const QByteArray key2 = E2eeCrypto::deriveMessageKey(dh2 + dh2);
    QCOMPARE(E2eeCrypto::aesGcmDecrypt(key2, decoded[0].iv, decoded[0].ciphertext),
             message.toUtf8());
}

// 模拟完整发送方/接收方流程：预密钥模式密钥协商 + 逐消息加密
void TestEncryptionManager::fullE2eeMessageFlow()
{
    // 接收方：身份密钥 + 一次性预密钥（服务端仅存公钥）
    const auto receiverIdentity = E2eeCrypto::generateX25519KeyPair();
    const auto receiverPrekey = E2eeCrypto::generateX25519KeyPair();

    // 发送方：拉取公钥后生成临时密钥对并加密
    const auto senderEph = E2eeCrypto::generateX25519KeyPair();
    QByteArray senderShared = E2eeCrypto::ecdh(senderEph.privateKey, receiverPrekey.publicKey);
    senderShared += E2eeCrypto::ecdh(senderEph.privateKey, receiverIdentity.publicKey);
    const QByteArray senderKey = E2eeCrypto::deriveMessageKey(senderShared);

    const QString message = QStringLiteral("端到端加密消息 ✓");
    const auto gcm = E2eeCrypto::aesGcmEncrypt(senderKey, message.toUtf8());
    QVERIFY(gcm.valid);

    // 接收方：用预密钥私钥 + 身份私钥推导相同密钥并解密
    QByteArray receiverShared = E2eeCrypto::ecdh(receiverPrekey.privateKey, senderEph.publicKey);
    receiverShared += E2eeCrypto::ecdh(receiverIdentity.privateKey, senderEph.publicKey);
    const QByteArray receiverKey = E2eeCrypto::deriveMessageKey(receiverShared);

    QCOMPARE(senderKey, receiverKey);
    QCOMPARE(E2eeCrypto::aesGcmDecrypt(receiverKey, gcm.iv, gcm.ciphertext),
             message.toUtf8());
}

QTEST_MAIN(TestEncryptionManager)
#include "TestEncryptionManager.moc"
