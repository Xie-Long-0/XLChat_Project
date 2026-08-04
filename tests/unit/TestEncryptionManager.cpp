#include <QtTest/QtTest>
#include <QRegularExpression>

#include "EncryptionManager.h"

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

QTEST_MAIN(TestEncryptionManager)
#include "TestEncryptionManager.moc"
