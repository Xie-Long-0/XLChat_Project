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

    // M3 联系人测试
    void addAndListContacts();
    void contactIsBidirectional();

    // M3 会话与消息测试
    void createPrivateConversation();
    void sendAndRetrieveMessages();
    void syncMessagesAfterId();
    void unreadCountWorks();
    void markMessagesAsRead();

    // M5.5 安全加固测试
    void v4TablesExist();
    void sessionByIdContainsTokenHash();
    void conversationMembershipAuthorization();
    void messageAccessAuthorization();
    void clientMessageIdDeduplicates();
    void receiptsAggregatePerRecipient();
    void readCursorOnlyMovesForward();
    void syncEventsCursorWorks();

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

// ── 迁移 ──
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
    QVERIFY(tables.contains("contacts"));
    QVERIFY(tables.contains("conversations"));
    QVERIFY(tables.contains("conversation_members"));
    QVERIFY(tables.contains("messages"));
    QVERIFY(tables.contains("schema_version"));
}

void TestDatabaseManager::v4TablesExist()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    QVERIFY(q.exec("SELECT name FROM sqlite_master WHERE type='table'"));
    QStringList tables;
    while (q.next()) {
        tables << q.value(0).toString();
    }
    QVERIFY(tables.contains("message_receipts"));
    QVERIFY(tables.contains("sync_events"));
}

void TestDatabaseManager::sessionByIdContainsTokenHash()
{
    auto user = m_db->getUserByUsername("testuser");
    QVERIFY(user.has_value());

    const qint64 sid = m_db->createSession(user->id, "dev-renew",
                                           "tokenhash-renew", "127.0.0.1");
    QVERIFY(sid > 0);

    auto session = m_db->getSessionById(sid);
    QVERIFY(session.has_value());
    QCOMPARE(session->tokenHash, QString("tokenhash-renew"));
    QCOMPARE(session->deviceId, QString("dev-renew"));
}

void TestDatabaseManager::conversationMembershipAuthorization()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    // 局外用户
    const qint64 outsiderId = m_db->registerUser("outsider", "", "", "hash3");
    QVERIFY(outsiderId > 0);

    const qint64 convId = m_db->getOrCreatePrivateConversation(user1->id, user2->id);
    QVERIFY(convId > 0);

    // 成员可访问，非成员被拒绝
    QVERIFY(m_db->isConversationMember(convId, user1->id));
    QVERIFY(m_db->isConversationMember(convId, user2->id));
    QVERIFY(!m_db->isConversationMember(convId, outsiderId));
}

void TestDatabaseManager::messageAccessAuthorization()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    auto outsider = m_db->getUserByUsername("outsider");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());
    QVERIFY(outsider.has_value());

    const qint64 convId = m_db->getOrCreatePrivateConversation(user1->id, user2->id);
    const qint64 msgId = m_db->sendMessage(convId, user1->id, "auth check msg");
    QVERIFY(msgId > 0);

    QVERIFY(m_db->canAccessMessage(msgId, user1->id));
    QVERIFY(m_db->canAccessMessage(msgId, user2->id));
    QVERIFY(!m_db->canAccessMessage(msgId, outsider->id));
}

void TestDatabaseManager::clientMessageIdDeduplicates()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    const qint64 convId = m_db->getOrCreatePrivateConversation(user1->id, user2->id);

    // 首次发送
    const qint64 first = m_db->sendMessage(convId, user1->id, "retry-safe",
                                           "text", "client-key-1", "deviceA");
    QVERIFY(first > 0);

    // 同一设备重试相同幂等键：返回同一消息，不重复写入
    const qint64 retry = m_db->sendMessage(convId, user1->id, "retry-safe",
                                           "text", "client-key-1", "deviceA");
    QCOMPARE(retry, first);

    // 不同设备相同幂等键视为不同消息
    const qint64 otherDevice = m_db->sendMessage(convId, user1->id, "other device",
                                                 "text", "client-key-1", "deviceB");
    QVERIFY(otherDevice > 0);
    QVERIFY(otherDevice != first);

    auto byKey = m_db->getMessageByClientKey(user1->id, "deviceA", "client-key-1");
    QVERIFY(byKey.has_value());
    QCOMPARE(byKey->id, first);
}

void TestDatabaseManager::receiptsAggregatePerRecipient()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    const qint64 convId = m_db->getOrCreatePrivateConversation(user1->id, user2->id);
    const qint64 msgId = m_db->sendMessage(convId, user1->id, "receipt test");
    QVERIFY(msgId > 0);

    QCOMPARE(m_db->receiptCount(msgId, "delivered"), 0);
    QCOMPARE(m_db->receiptCount(msgId, "read"), 0);

    // user2 的两台设备先后送达
    QVERIFY(m_db->recordMessageReceipt(msgId, user2->id, "dev1", "delivered"));
    QVERIFY(m_db->recordMessageReceipt(msgId, user2->id, "dev2", "delivered"));
    QCOMPARE(m_db->receiptCount(msgId, "delivered"), 2);
    QCOMPARE(m_db->receiptCount(msgId, "read"), 0);

    // 其中一台已读
    QVERIFY(m_db->recordMessageReceipt(msgId, user2->id, "dev1", "read"));
    QCOMPARE(m_db->receiptCount(msgId, "read"), 1);

    // 重复回执不重复计数
    QVERIFY(m_db->recordMessageReceipt(msgId, user2->id, "dev1", "read"));
    QCOMPARE(m_db->receiptCount(msgId, "read"), 1);

    // 非法状态被拒绝
    QVERIFY(!m_db->recordMessageReceipt(msgId, user2->id, "dev1", "bogus"));
}

void TestDatabaseManager::readCursorOnlyMovesForward()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    const qint64 convId = m_db->getOrCreatePrivateConversation(user1->id, user2->id);
    const qint64 m1 = m_db->sendMessage(convId, user1->id, "cursor msg 1");
    const qint64 m2 = m_db->sendMessage(convId, user1->id, "cursor msg 2");
    QVERIFY(m2 > m1);

    QVERIFY(m_db->updateMemberReadCursor(convId, user2->id, m2));
    // 回退到更早的消息不应生效
    QVERIFY(m_db->updateMemberReadCursor(convId, user2->id, m1));

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT last_read_message_id FROM conversation_members "
              "WHERE conversation_id = ? AND user_id = ?");
    q.addBindValue(convId);
    q.addBindValue(user2->id);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toLongLong(), m2);
}

void TestDatabaseManager::syncEventsCursorWorks()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    QVERIFY(m_db->appendSyncEvent(user1->id, "message", "{\"a\":1}") > 0);
    QVERIFY(m_db->appendSyncEvent(user1->id, "receipt", "{\"b\":2}") > 0);
    // 他人事件不可见
    QVERIFY(m_db->appendSyncEvent(user2->id, "message", "{\"c\":3}") > 0);

    auto all = m_db->getSyncEvents(user1->id, 0);
    QCOMPARE(all.size(), 2);
    QCOMPARE(all[0].eventType, QString("message"));
    QCOMPARE(all[1].eventType, QString("receipt"));
    QVERIFY(all[1].seq > all[0].seq);

    // 游标之后无新事件
    auto none = m_db->getSyncEvents(user1->id, all.last().seq);
    QCOMPARE(none.size(), 0);

    // limit 生效
    auto limited = m_db->getSyncEvents(user1->id, 0, 1);
    QCOMPARE(limited.size(), 1);
}

// ── 用户管理 ──
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

// ── Session ──
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

// ── 登录审计 ──
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

// ── 设备管理 ──
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

// ── 联系人 ──
void TestDatabaseManager::addAndListContacts()
{
    // 注册第二个用户
    const qint64 id2 = m_db->registerUser("user2", "u2@test.com", "", "hash2");
    QVERIFY(id2 > 0);

    auto user1 = m_db->getUserByUsername("testuser");
    QVERIFY(user1.has_value());

    // 添加联系人
    QVERIFY(m_db->addContact(user1->id, id2));

    auto contacts = m_db->getContacts(user1->id);
    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts[0].contactUserId, id2);
    QCOMPARE(contacts[0].contactUsername, "user2");
}

void TestDatabaseManager::contactIsBidirectional()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    // user2 的联系人列表也应包含 user1
    auto contacts2 = m_db->getContacts(user2->id);
    QCOMPARE(contacts2.size(), 1);
    QCOMPARE(contacts2[0].contactUserId, user1->id);

    QVERIFY(m_db->isContact(user1->id, user2->id));
    QVERIFY(m_db->isContact(user2->id, user1->id));
}

// ── M3 会话与消息 ──
void TestDatabaseManager::createPrivateConversation()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    const qint64 convId = m_db->getOrCreatePrivateConversation(user1->id, user2->id);
    QVERIFY(convId > 0);

    // 重复调用返回相同会话
    const qint64 convId2 = m_db->getOrCreatePrivateConversation(user1->id, user2->id);
    QCOMPARE(convId2, convId);

    auto convs = m_db->getConversationsForUser(user1->id);
    QCOMPARE(convs.size(), 1);
    QCOMPARE(convs[0].peerUserId, user2->id);
    QCOMPARE(convs[0].peerUsername, "user2");
}

void TestDatabaseManager::sendAndRetrieveMessages()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    const qint64 convId = m_db->getOrCreatePrivateConversation(user1->id, user2->id);

    // 发送消息
    const qint64 msg1 = m_db->sendMessage(convId, user1->id, "Hello!");
    const qint64 msg2 = m_db->sendMessage(convId, user2->id, "Hi there!");
    QVERIFY(msg1 > 0);
    QVERIFY(msg2 > 0);
    QVERIFY(msg2 > msg1);

    // 获取消息
    auto messages = m_db->getMessages(convId);
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages[0].content, "Hello!");
    QCOMPARE(messages[0].senderId, user1->id);
    QCOMPARE(messages[1].content, "Hi there!");
    QCOMPARE(messages[1].senderId, user2->id);
}

void TestDatabaseManager::syncMessagesAfterId()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    const qint64 convId = m_db->getOrCreatePrivateConversation(user1->id, user2->id);
    auto allMsgs = m_db->getMessages(convId);
    QVERIFY(allMsgs.size() >= 2);

    const qint64 afterId = allMsgs.first().id;
    auto synced = m_db->syncMessages(convId, afterId);
    // 应该只返回 afterId 之后的消息
    for (const auto &m : synced) {
        QVERIFY(m.id > afterId);
    }
}

void TestDatabaseManager::unreadCountWorks()
{
    auto user1 = m_db->getUserByUsername("testuser");
    auto user2 = m_db->getUserByUsername("user2");
    QVERIFY(user1.has_value());
    QVERIFY(user2.has_value());

    const qint64 convId = m_db->getOrCreatePrivateConversation(user1->id, user2->id);

    // user2 发送消息给 user1
    m_db->sendMessage(convId, user2->id, "msg for user1");

    const int unread = m_db->getUnreadCount(convId, user1->id);
    QVERIFY(unread >= 1);
}

void TestDatabaseManager::markMessagesAsRead()
{
    auto user1 = m_db->getUserByUsername("testuser");
    QVERIFY(user1.has_value());

    auto convs = m_db->getConversationsForUser(user1->id);
    QVERIFY(!convs.isEmpty());
    const qint64 convId = convs[0].id;

    // 标记为已读
    QVERIFY(m_db->updateMessagesReadStatus(convId, user1->id));

    // 已读数应为 0
    const int unread = m_db->getUnreadCount(convId, user1->id);
    QCOMPARE(unread, 0);
}

QTEST_MAIN(TestDatabaseManager)
#include "TestDatabaseManager.moc"
