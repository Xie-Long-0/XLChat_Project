#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSslCertificate>
#include <QSslKey>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "LogSanitizer.h"
#include "SecureMemory.h"
#include "TlsHelper.h"
#include "NonceCache.h"
#include "RateWindow.h"
#include "StructuredLogger.h"

using namespace XYChat::Security;
using XYChat::Server::RateWindow;

class TestSecurity : public QObject
{
    Q_OBJECT

private slots:
    // LogSanitizer 测试
    void testMaskPassword();
    void testMaskToken();
    void testMaskMessageContent();
    void testMaskIpAddress();
    void testMaskEmail();
    void testMaskGeneric();

    // SecureMemory 测试
    void testWipeByteArray();
    void testWipeString();

    // TlsHelper 测试
    void testGenerateDevCertificates();
    void testLoadServerConfig();
    void testLoadClientConfig();

    // M5.5: NonceCache 重放保护测试
    void testNonceAcceptsFreshAndRejectsDuplicate();
    void testNonceRejectsEmpty();
    void testNonceExpiresAfterTtl();

    // M11 前置: 连接级限流窗口测试
    void testRateWindowAllowsUpToLimitThenRejects();
    void testRateWindowResetsAfterWindow();

    // M11 前置: 结构化日志测试
    void testStructuredLoggerJsonFields();
    void testStructuredLoggerSanitizesSensitive();
};

// LogSanitizer

void TestSecurity::testMaskPassword()
{
    QCOMPARE(LogSanitizer::maskPassword("mysecretpass"), QString("my***"));
    QCOMPARE(LogSanitizer::maskPassword("ab"), QString("***"));
    QCOMPARE(LogSanitizer::maskPassword(""), QString("[empty]"));
    QCOMPARE(LogSanitizer::maskPassword("a"), QString("***"));
}

void TestSecurity::testMaskToken()
{
    const QString token = "abcdef1234567890abcdef1234567890";
    const QString masked = LogSanitizer::maskToken(token);
    QVERIFY(masked.startsWith("abcdef12"));
    QVERIFY(masked.endsWith("..."));
    QVERIFY(!masked.contains(token)); // 完整 token 不应出现

    QCOMPARE(LogSanitizer::maskToken(""), QString("[empty]"));
    QCOMPARE(LogSanitizer::maskToken("short"), QString("[token]"));
}

void TestSecurity::testMaskMessageContent()
{
    const QString longMsg = "This is a very long message that should be truncated in logs";
    const QString masked = LogSanitizer::maskMessageContent(longMsg);
    QVERIFY(masked.size() < longMsg.size());
    QVERIFY(masked.endsWith("..."));

    QCOMPARE(LogSanitizer::maskMessageContent(""), QString("[empty]"));
}

void TestSecurity::testMaskIpAddress()
{
    QCOMPARE(LogSanitizer::maskIpAddress("192.168.1.100"), QString("192.168.*.*"));
    QCOMPARE(LogSanitizer::maskIpAddress("10.0.0.1"), QString("10.0.*.*"));
    // IPv6 或其他格式
    const QString ipv6 = "2001:0db8:85a3:0000:0000:8a2e:0370:7334";
    QVERIFY(LogSanitizer::maskIpAddress(ipv6).endsWith("..."));
}

void TestSecurity::testMaskEmail()
{
    QCOMPARE(LogSanitizer::maskEmail("user@example.com"), QString("us***@example.com"));
    QCOMPARE(LogSanitizer::maskEmail("a@b.com"), QString("[email]"));
}

void TestSecurity::testMaskGeneric()
{
    QCOMPARE(LogSanitizer::mask("sensitive_data", 4), QString("sens..."));
    QCOMPARE(LogSanitizer::mask("abc", 4), QString("[masked]"));
    QCOMPARE(LogSanitizer::mask("", 4), QString("[empty]"));
}

// SecureMemory

void TestSecurity::testWipeByteArray()
{
    QByteArray data = "sensitive_key_material_here";
    QVERIFY(!data.isEmpty());
    SecureMemory::wipe(data);
    QVERIFY(data.isEmpty());
}

void TestSecurity::testWipeString()
{
    QString str = "my_secret_password";
    QVERIFY(!str.isEmpty());
    SecureMemory::wipe(str);
    QVERIFY(str.isEmpty());
}

// TlsHelper

void TestSecurity::testGenerateDevCertificates()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const bool result = TlsHelper::generateDevCertificates(tempDir.path());
    QVERIFY(result);

    // 验证文件存在
    QVERIFY(QFile::exists(tempDir.path() + "/ca.key"));
    QVERIFY(QFile::exists(tempDir.path() + "/ca.crt"));
    QVERIFY(QFile::exists(tempDir.path() + "/server.key"));
    QVERIFY(QFile::exists(tempDir.path() + "/server.crt"));

    // 验证 CA 证书可被 Qt 解析
    QFile caFile(tempDir.path() + "/ca.crt");
    QVERIFY(caFile.open(QIODevice::ReadOnly));
    QSslCertificate caCert(caFile.readAll(), QSsl::Pem);
    caFile.close();
    QVERIFY(!caCert.isNull());

    // 验证服务端证书可被 Qt 解析
    QFile serverFile(tempDir.path() + "/server.crt");
    QVERIFY(serverFile.open(QIODevice::ReadOnly));
    QSslCertificate serverCert(serverFile.readAll(), QSsl::Pem);
    serverFile.close();
    QVERIFY(!serverCert.isNull());

    // 验证服务端私钥可被 Qt 解析
    QFile keyFile(tempDir.path() + "/server.key");
    QVERIFY(keyFile.open(QIODevice::ReadOnly));
    QSslKey serverKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    keyFile.close();
    QVERIFY(!serverKey.isNull());
}

void TestSecurity::testLoadServerConfig()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // 先生成证书
    QVERIFY(TlsHelper::generateDevCertificates(tempDir.path()));

    // 加载配置
    auto config = TlsHelper::loadServerConfig(
        tempDir.path() + "/server.crt",
        tempDir.path() + "/server.key",
        tempDir.path() + "/ca.crt",
        false);

    QVERIFY(config.valid);
    QVERIFY(!config.certificate.isNull());
    QVERIFY(!config.privateKey.isNull());
}

void TestSecurity::testLoadClientConfig()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // 先生成证书
    QVERIFY(TlsHelper::generateDevCertificates(tempDir.path()));

    // 加载客户端配置
    auto config = TlsHelper::loadClientConfig(tempDir.path() + "/ca.crt");
    QVERIFY(config.valid);
    QVERIFY(!config.caCertificate.isNull());

    // 不存在的 CA 文件应返回无效
    auto invalidConfig = TlsHelper::loadClientConfig("/nonexistent/ca.crt");
    QVERIFY(!invalidConfig.valid);
}

// M5.5: NonceCache 重放保护
void TestSecurity::testNonceAcceptsFreshAndRejectsDuplicate()
{
    NonceCache cache(600);
    QVERIFY(cache.checkAndInsert("nonce-1"));
    // 重复 nonce 必须拒绝（防重放）
    QVERIFY(!cache.checkAndInsert("nonce-1"));
    // 不同 nonce 正常接受
    QVERIFY(cache.checkAndInsert("nonce-2"));
    QCOMPARE(cache.size(), 2);
}

void TestSecurity::testNonceRejectsEmpty()
{
    NonceCache cache(600);
    // 缺失/空 nonce 一律拒绝（必填语义）
    QVERIFY(!cache.checkAndInsert(QString()));
    QCOMPARE(cache.size(), 0);
}

void TestSecurity::testNonceExpiresAfterTtl()
{
    // TTL=0：下一秒即可重新使用同一 nonce
    NonceCache cache(0);
    QVERIFY(cache.checkAndInsert("nonce-ttl"));
    QVERIFY(!cache.checkAndInsert("nonce-ttl"));
    QTest::qWait(1100);
    cache.purgeExpired();
    QVERIFY(cache.checkAndInsert("nonce-ttl"));
}

// M11 前置: 连接级限流窗口
void TestSecurity::testRateWindowAllowsUpToLimitThenRejects()
{
    RateWindow window(3, 10);
    const qint64 t0 = 1000;
    QVERIFY(window.allow(t0));       // 第 1 次
    QVERIFY(window.allow(t0 + 1));   // 第 2 次
    QVERIFY(window.allow(t0 + 2));   // 第 3 次
    QVERIFY(!window.allow(t0 + 3));  // 第 4 次仍在窗口内 -> 拒绝
    QCOMPARE(window.count(), 3);     // 被拒的调用不递增计数
}

void TestSecurity::testRateWindowResetsAfterWindow()
{
    RateWindow window(2, 10);
    const qint64 t0 = 5000;
    QVERIFY(window.allow(t0));
    QVERIFY(window.allow(t0));
    QVERIFY(!window.allow(t0 + 5));  // 5 < 10，仍在窗口内 -> 拒绝
    // 窗口过期（>= windowSeconds）后计数重置
    QVERIFY(window.allow(t0 + 10));
    QVERIFY(window.allow(t0 + 10));
    QVERIFY(!window.allow(t0 + 11));
}

// M11 前置: 结构化日志
void TestSecurity::testStructuredLoggerJsonFields()
{
    const QString json = StructuredLogger::event(LogLevel::Info, "response")
                             .requestId(42)
                             .userId(7)
                             .deviceId("dev-abc")
                             .field("type", "send_message")
                             .errorCode(0)
                             .durationMs(3)
                             .toJson();

    // 单行且为合法 JSON
    QVERIFY(!json.contains(QLatin1Char('\n')));
    QJsonParseError err;
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8(), &err).object();
    QCOMPARE(err.error, QJsonParseError::NoError);
    QCOMPARE(obj.value("event").toString(), QString("response"));
    QCOMPARE(obj.value("level").toString(), QString("info"));
    QVERIFY(obj.contains("ts"));
    QCOMPARE(obj.value("requestId").toVariant().toLongLong(), static_cast<qint64>(42));
    QCOMPARE(obj.value("userId").toVariant().toLongLong(), static_cast<qint64>(7));
    QCOMPARE(obj.value("deviceId").toString(), QString("dev-abc"));
    QCOMPARE(obj.value("type").toString(), QString("send_message"));
    QCOMPARE(obj.value("code").toVariant().toInt(), 0);
    QCOMPARE(obj.value("durationMs").toVariant().toLongLong(), static_cast<qint64>(3));
}

void TestSecurity::testStructuredLoggerSanitizesSensitive()
{
    const QString json = StructuredLogger::event(LogLevel::Warning, "auth.rate_limited")
                             .ipField("ip", "192.168.1.100")
                             .tokenField("token", "abcdef1234567890abcdef")
                             .contentField("content",
                                           "a very long secret message body that must be masked")
                             .toJson();

    // IP 脱敏：保留前两段，完整 IP 不出现
    QVERIFY(json.contains("192.168.*.*"));
    QVERIFY(!json.contains("192.168.1.100"));
    // token 脱敏：完整 token 不出现，仅前 8 字符 + ...
    QVERIFY(!json.contains("abcdef1234567890abcdef"));
    QVERIFY(json.contains("abcdef12..."));
    // 正文脱敏：完整正文不出现
    QVERIFY(!json.contains("a very long secret message body that must be masked"));
}

QTEST_MAIN(TestSecurity)
#include "TestSecurity.moc"
