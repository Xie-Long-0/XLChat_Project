#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "database/DatabaseManager.h"

class TestDatabaseManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 迁移测试
    void migrationCreatesAllTables();

    // 用户管理测试
    void registerAndRetrieveUser();
    void duplicateUsernameFails();
    void userExistsReturnsCorrectly();

    // Session 测试
    void createAndRetrieveSession();
    void deleteSessionWorks();
    void sessionExpiryIsSet();

    // 登录审计测试
    void recordAndCountFailedLogins();

    // 设备管理测试
    void registerAndListDevices();

private:
    DatabaseManager *m_db = nullptr;
    QString m_connectionName;
};

void TestDatabaseManager::initTestCase()
{
    m_connectionName = QString("test_db_%1").arg(QDateTime::currentMSecsSinceEpoch());
    m_db = new DatabaseManager(m_connectionName);

    // 使用内存数据库方便测试
    // 先手动打开
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(":memory:");
    QVERIFY(db.open());

    // 运行迁移
    QVERIFY(m_db->initialize());
}

void TestDatabaseManager::cleanupTestCase()
{
    delete m_db;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

// ── 迁移 ─────────────────────────────────────────────────────────────────────
void TestDatabaseManager::migrationCreatesAllTables()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 检查所有表存在
    QVERIFY(q.exec("SELECT name FROM sqlite_master WHERE type='table'"));
    QStringList tables;
    while (q.next()) {
        tables << q.value(0).toString();
    }

    QVERIFY(tables.contains("users"));
    QVERIFY(tables.contains("devices"));
    QVERIFY(tables.contains("sessions"));
    QVERIFY(tables.contains("login_audit"));
    QVERIFY(tables.contains("schema_version"));
}

// ── 用户管理 ─────────────────────────────────────────────────────────────────
void TestDatabaseManager::registerAndRetrieveUser()
{
    const qint64 id = m_db->registerUser(
        "testuser",
        "test@example.com",
        "13800000000",
        "v1:100000:aabb:aabb");

    QVERIFY(id > 0);

    auto user = m_db->getUserByUsername("testuser");
    QVERIFY(user.has_value());
    QCOMPARE(user->username, "testuser");
    QCOMPARE(user->email, "test@example.com");
    QCOMPARE(user->phone, "13800000000");
}

void TestDatabaseManager::duplicateUsernameFails()
{
    const qint64 id = m_db->registerUser(
        "testuser", {}, {}, "hash");
    QCOMPARE(id, static_cast<qint64>(-1));
}

void TestDatabaseManager::userExistsReturnsCorrectly()
{
    QVERIFY(m_db->userExists("testuser"));
    QVERIFY(!m_db->userExists("nonexistent"));
}

// ── Session ──────────────────────────────────────────────────────────────────
void TestDatabaseManager::createAndRetrieveSession()
{
    auto user = m_db->getUserByUsername("testuser");
    QVERIFY(user.has_value());

    const qint64 sessionId = m_db->createSession(
        user->id, "device1", "tokenhash1",
        "127.0.0.1");

    QVERIFY(sessionId > 0);

    auto session = m_db->getSessionByTokenHash("tokenhash1");
    QVERIFY(session.has_value());
    QCOMPARE(session->userId, user->id);
    QCOMPARE(session->deviceId, "device1");
}

void TestDatabaseManager::deleteSessionWorks()
{
    auto user = m_db->getUserByUsername("testuser");
    QVERIFY(user.has_value());

    const qint64 sid = m_db->createSession(
        user->id, "device-del", "token-del",
        "127.0.0.1");
    QVERIFY(sid > 0);

    QVERIFY(m_db->deleteSession(sid));
    auto session = m_db->getSessionByTokenHash("token-del");
    QVERIFY(!session.has_value());
}

void TestDatabaseManager::sessionExpiryIsSet()
{
    auto user = m_db->getUserByUsername("testuser");
    QVERIFY(user.has_value());

    const qint64 sid = m_db->createSession(
        user->id, "device-exp", "token-exp",
        "127.0.0.1", 3600);
    QVERIFY(sid > 0);

    auto session = m_db->getSessionByTokenHash("token-exp");
    QVERIFY(session.has_value());
    QVERIFY(!session->expiresAt.isEmpty());
}

// ── 登录审计 ─────────────────────────────────────────────────────────────────
void TestDatabaseManager::recordAndCountFailedLogins()
{
    auto user = m_db->getUserByUsername("testuser");
    QVERIFY(user.has_value());

    // 记录 3 次失败
    for (int i = 0; i < 3; ++i) {
        m_db->recordLoginAttempt(user->id, "192.168.1.1",
                                 false, "Wrong password");
    }

    const int ipCount = m_db->recentFailedLoginCount("192.168.1.1");
    QCOMPARE(ipCount, 3);

    const int userCount = m_db->recentFailedLoginCountForUser(user->id);
    QCOMPARE(userCount, 3);

    // 成功的不计入
    m_db->recordLoginAttempt(user->id, "192.168.1.1", true);
    QCOMPARE(m_db->recentFailedLoginCount("192.168.1.1"), 3);
}

// ── 设备管理 ─────────────────────────────────────────────────────────────────
void TestDatabaseManager::registerAndListDevices()
{
    auto user = m_db->getUserByUsername("testuser");
    QVERIFY(user.has_value());

    QVERIFY(m_db->registerDevice(user->id, "dev-001",
                                 "My Phone", "android"));

    auto devices = m_db->getDevicesByUserId(user->id);
    QCOMPARE(devices.size(), 1);
    QCOMPARE(devices[0].value("deviceId").toString(), "dev-001");
    QCOMPARE(devices[0].value("platform").toString(), "android");

    // UPSERT：重复注册同一设备不会增加数量
    QVERIFY(m_db->registerDevice(user->id, "dev-001",
                                 "My Phone Updated", "android"));
    devices = m_db->getDevicesByUserId(user->id);
    QCOMPARE(devices.size(), 1);
}

QTEST_MAIN(TestDatabaseManager)
#include "TestDatabaseManager.moc"
