#include <QtTest/QtTest>
#include <QSet>
#include <QDateTime>
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
    // P1 安全加固（2026-09-02）：validateSession 逐请求回查 sessions 表依赖的会话契约
    void sessionByIdReflectsDeletionAndExpiry();
    void conversationMembershipAuthorization();
    void messageAccessAuthorization();
    void clientMessageIdDeduplicates();
    void receiptsAggregatePerRecipient();
    void readCursorOnlyMovesForward();
    void syncEventsCursorWorks();

    // M6 端到端加密密钥管理测试
    void v5TablesExist();
    void identityKeyUpsertAndRetrieve();
    void prekeyUploadAndCount();
    void prekeyClaimIsOncePerDevice();
    void claimedPrekeyValidationAndConsumption();
    void removeDeviceClearsKeyMaterial();
    void identityKeyChangePurgesStalePrekeys();

    // M7a 群聊数据层测试
    void groupMigrationAddsNameAndRoleColumns();
    void createGroupInsertsOwnerAndMembers();
    void addGroupMembersSkipsDuplicates();
    void removeGroupMemberDeletesRow();
    void groupRoleReportsOwnershipAndNonMember();
    void getConversationsForUserIncludesGroupWithNameAndMemberCount();
    // M7a 子任务二：fan-out 与回执聚合数据支撑
    void groupMemberIdsAndCounts();
    void groupMessageReceiptCounts();

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

// 迁移
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

// P1 安全加固（2026-09-02）：validateSession 逐请求回查 sessions 表，
// 依赖两项契约——会话删除后 getSessionById 立即返回空（登出/终止/续期换代即失效），
// 且 expiresAt 以可解析的 ISO 格式存储并落在未来（过期判定不会 fail-open）。
void TestDatabaseManager::sessionByIdReflectsDeletionAndExpiry()
{
    auto user = m_db->getUserByUsername("testuser");
    QVERIFY(user.has_value());

    const qint64 sid = m_db->createSession(user->id, "dev-p1",
                                           "tokenhash-p1", "127.0.0.1", 3600);
    QVERIFY(sid > 0);

    // 契约一：expiresAt 可被 Qt::ISODate 解析且在未来（新鲜会话未过期）
    auto session = m_db->getSessionById(sid);
    QVERIFY(session.has_value());
    QCOMPARE(session->userId, user->id);
    const QDateTime expiresAt = QDateTime::fromString(session->expiresAt, Qt::ISODate);
    QVERIFY(expiresAt.isValid());
    QVERIFY(expiresAt > QDateTime::currentDateTimeUtc());

    // 契约二：删除会话后 getSessionById 立即返回空（token 被终止/登出后存量连接失效）
    QVERIFY(m_db->deleteSession(sid));
    QVERIFY(!m_db->getSessionById(sid).has_value());
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

// 用户管理
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

// Session
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

// 登录审计
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

// 设备管理
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

// 联系人
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

// M3 会话与消息
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

// M6: 端到端加密密钥管理
void TestDatabaseManager::v5TablesExist()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    QVERIFY(q.exec("SELECT name FROM sqlite_master WHERE type='table'"));
    QStringList tables;
    while (q.next()) {
        tables << q.value(0).toString();
    }
    QVERIFY(tables.contains("device_identity_keys"));
    QVERIFY(tables.contains("prekeys"));
}

void TestDatabaseManager::identityKeyUpsertAndRetrieve()
{
    const qint64 userId = m_db->registerUser("e2eeuser", "", "", "hash-e2ee");
    QVERIFY(userId > 0);

    QVERIFY(m_db->upsertIdentityKey(userId, "dev-a", "pubA"));
    auto keys = m_db->getIdentityKeysByUser(userId);
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys[0].deviceId, "dev-a");
    QCOMPARE(keys[0].identityPub, "pubA");

    // UPSERT：同设备更新公钥不新增记录
    QVERIFY(m_db->upsertIdentityKey(userId, "dev-a", "pubA2"));
    keys = m_db->getIdentityKeysByUser(userId);
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys[0].identityPub, "pubA2");

    // 空参数被拒绝
    QVERIFY(!m_db->upsertIdentityKey(userId, "", "pub"));
    QVERIFY(!m_db->upsertIdentityKey(userId, "dev-a", ""));
}

void TestDatabaseManager::prekeyUploadAndCount()
{
    auto user = m_db->getUserByUsername("e2eeuser");
    QVERIFY(user.has_value());

    QCOMPARE(m_db->prekeyCount(user->id, "dev-a"), 0);
    QCOMPARE(m_db->uploadPrekeys(user->id, "dev-a", {"pk1", "pk2", "pk3"}), 3);
    QCOMPARE(m_db->prekeyCount(user->id, "dev-a"), 3);

    // 非法参数
    QCOMPARE(m_db->uploadPrekeys(user->id, "", {"pk"}), -1);
    QCOMPARE(m_db->uploadPrekeys(user->id, "dev-a", {}), -1);
}

void TestDatabaseManager::prekeyClaimIsOncePerDevice()
{
    auto user = m_db->getUserByUsername("e2eeuser");
    QVERIFY(user.has_value());

    // 每设备认领一个：再加一台设备验证多设备各认领一个
    m_db->uploadPrekeys(user->id, "dev-b", {"pk-b1"});

    auto claimed = m_db->claimPrekeys(user->id);
    QCOMPARE(claimed.size(), 2);
    QSet<QString> claimedDevices;
    qint64 firstDevAPrekeyId = 0;
    for (const auto &c : claimed) {
        claimedDevices.insert(c.deviceId);
        QVERIFY(!c.prekeyPub.isEmpty());
        QVERIFY(c.prekeyId > 0);
        if (c.deviceId == "dev-a") firstDevAPrekeyId = c.prekeyId;
    }
    QVERIFY(claimedDevices.contains("dev-a"));
    QVERIFY(claimedDevices.contains("dev-b"));
    QVERIFY(firstDevAPrekeyId > 0);

    // 认领后余量递减（dev-a 3-1=2，dev-b 1-1=0）
    QCOMPARE(m_db->prekeyCount(user->id, "dev-a"), 2);
    QCOMPARE(m_db->prekeyCount(user->id, "dev-b"), 0);

    // 再次认领：dev-b 无库存，只剩 dev-a，且不会重复认领同一预密钥
    auto claimed2 = m_db->claimPrekeys(user->id);
    QCOMPARE(claimed2.size(), 1);
    QCOMPARE(claimed2[0].deviceId, "dev-a");
    QVERIFY(claimed2[0].prekeyId != firstDevAPrekeyId);

    // 同一预密钥不会被两次认领：继续认领直到耗尽
    auto claimed3 = m_db->claimPrekeys(user->id);
    QCOMPARE(claimed3.size(), 1);
    auto claimed4 = m_db->claimPrekeys(user->id);
    QVERIFY(claimed4.isEmpty());
}

void TestDatabaseManager::claimedPrekeyValidationAndConsumption()
{
    auto user = m_db->getUserByUsername("e2eeuser");
    QVERIFY(user.has_value());

    m_db->uploadPrekeys(user->id, "dev-a", {"pk-v1"});
    auto claimed = m_db->claimPrekeys(user->id);
    QVERIFY(!claimed.isEmpty());
    const ClaimedPrekey c = claimed.last();

    // claimed 状态可校验通过
    QVERIFY(m_db->validateClaimedPrekey(user->id, c.deviceId, c.prekeyId));
    // 错误的设备/用户/ID 被拒绝
    QVERIFY(!m_db->validateClaimedPrekey(user->id, "dev-x", c.prekeyId));
    QVERIFY(!m_db->validateClaimedPrekey(user->id + 999, c.deviceId, c.prekeyId));
    QVERIFY(!m_db->validateClaimedPrekey(user->id, c.deviceId, c.prekeyId + 999));

    // 消费后（used）不再可校验
    QCOMPARE(m_db->consumePrekeys({c.prekeyId}), 1);
    QVERIFY(!m_db->validateClaimedPrekey(user->id, c.deviceId, c.prekeyId));
    // 重复消费返回 0
    QCOMPARE(m_db->consumePrekeys({c.prekeyId}), 0);
}

void TestDatabaseManager::removeDeviceClearsKeyMaterial()
{
    auto user = m_db->getUserByUsername("e2eeuser");
    QVERIFY(user.has_value());

    m_db->registerDevice(user->id, "dev-c", "Phone", "android");
    m_db->upsertIdentityKey(user->id, "dev-c", "pubC");
    m_db->uploadPrekeys(user->id, "dev-c", {"pk-c1", "pk-c2"});
    QCOMPARE(m_db->prekeyCount(user->id, "dev-c"), 2);

    // 删除设备后密钥材料全部清除，无法再认领
    QVERIFY(m_db->removeDevice(user->id, "dev-c"));
    QCOMPARE(m_db->prekeyCount(user->id, "dev-c"), 0);
    bool found = false;
    for (const auto &k : m_db->getIdentityKeysByUser(user->id)) {
        if (k.deviceId == "dev-c") found = true;
    }
    QVERIFY(!found);
    QVERIFY(m_db->claimPrekeys(user->id).isEmpty());
    QVERIFY(m_db->removeDeviceKeys(user->id, "dev-c")); // 幂等删除
}

void TestDatabaseManager::identityKeyChangePurgesStalePrekeys()
{
    // 审查修复回归：身份公钥变更时，旧世代未消费预密钥必须废弃，
    // 否则发送方会认领到接收方无法解密的旧预密钥导致消息静默丢失
    const qint64 userId = m_db->registerUser("e2eerotate", "", "", "hash-rotate");
    QVERIFY(userId > 0);

    QVERIFY(m_db->upsertIdentityKey(userId, "dev-r", "gen1-pub"));
    QCOMPARE(m_db->uploadPrekeys(userId, "dev-r", {"gen1-pk1", "gen1-pk2"}), 2);
    QCOMPARE(m_db->prekeyCount(userId, "dev-r"), 2);

    // 相同公钥重复注册不清除预密钥
    QVERIFY(m_db->upsertIdentityKey(userId, "dev-r", "gen1-pub"));
    QCOMPARE(m_db->prekeyCount(userId, "dev-r"), 2);

    // 身份变更后旧预密钥全部废弃
    QVERIFY(m_db->upsertIdentityKey(userId, "dev-r", "gen2-pub"));
    QCOMPARE(m_db->prekeyCount(userId, "dev-r"), 0);
    QVERIFY(m_db->claimPrekeys(userId).isEmpty());

    // 新世代预密钥正常工作
    QCOMPARE(m_db->uploadPrekeys(userId, "dev-r", {"gen2-pk1"}), 1);
    QCOMPARE(m_db->claimPrekeys(userId).size(), 1);
}

// M7a: 群聊数据层
void TestDatabaseManager::groupMigrationAddsNameAndRoleColumns()
{
    // 主库：V7 列已存在
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    QVERIFY(q.exec("PRAGMA table_info(conversations)"));
    QStringList convCols;
    while (q.next()) {
        convCols << q.value(1).toString();
    }
    QVERIFY(convCols.contains("name"));

    QVERIFY(q.exec("PRAGMA table_info(conversation_members)"));
    QStringList memberCols;
    while (q.next()) {
        memberCols << q.value(1).toString();
    }
    QVERIFY(memberCols.contains("role"));

    // 模拟 V6 旧库：迁移后补齐列且存量数据完好
    const QString legacyConn = QString("test_v7_%1").arg(QDateTime::currentMSecsSinceEpoch());
    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase("QSQLITE", legacyConn);
        legacy.setDatabaseName(":memory:");
        QVERIFY(legacy.open());
        QSqlQuery lq(legacy);
        QVERIFY(lq.exec(
            "CREATE TABLE schema_version ("
            "  version INTEGER PRIMARY KEY,"
            "  applied_at TEXT NOT NULL DEFAULT (datetime('now')))"));
        QVERIFY(lq.exec("INSERT INTO schema_version (version) VALUES (6)"));
        QVERIFY(lq.exec(
            "CREATE TABLE conversations ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  type TEXT NOT NULL DEFAULT 'private',"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  updated_at TEXT NOT NULL DEFAULT (datetime('now')))"));
        QVERIFY(lq.exec(
            "CREATE TABLE conversation_members ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  conversation_id INTEGER NOT NULL,"
            "  user_id INTEGER NOT NULL,"
            "  joined_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  last_read_message_id INTEGER DEFAULT 0,"
            "  UNIQUE(conversation_id, user_id))"));
        QVERIFY(lq.exec("INSERT INTO conversations (type) VALUES ('private')"));
        QVERIFY(lq.exec("INSERT INTO conversation_members (conversation_id, user_id) VALUES (1, 1)"));
    }

    DatabaseManager legacyDb(legacyConn);
    QVERIFY(legacyDb.initialize());

    QSqlDatabase legacy = QSqlDatabase::database(legacyConn);
    QSqlQuery check(legacy);
    QVERIFY(check.exec("PRAGMA table_info(conversations)"));
    QStringList legacyConvCols;
    while (check.next()) {
        legacyConvCols << check.value(1).toString();
    }
    QVERIFY(legacyConvCols.contains("name"));

    QVERIFY(check.exec("PRAGMA table_info(conversation_members)"));
    QStringList legacyMemberCols;
    while (check.next()) {
        legacyMemberCols << check.value(1).toString();
    }
    QVERIFY(legacyMemberCols.contains("role"));

    // 存量成员数据完好，role 默认 member
    QVERIFY(check.exec("SELECT role, conversation_id, user_id FROM conversation_members"));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toString(), QString("member"));
    QCOMPARE(check.value(1).toLongLong(), 1LL);
    QCOMPARE(check.value(2).toLongLong(), 1LL);

    // 版本号推进到 7
    QVERIFY(check.exec("SELECT MAX(version) FROM schema_version"));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toInt(), 7);
}

void TestDatabaseManager::createGroupInsertsOwnerAndMembers()
{
    auto owner = m_db->getUserByUsername("testuser");
    auto member = m_db->getUserByUsername("user2");
    QVERIFY(owner.has_value());
    QVERIFY(member.has_value());

    // 重复、创建者自身与非法 ID 被自动过滤
    const qint64 convId = m_db->createGroup(owner->id, "项目群",
                                            {member->id, member->id, owner->id, -5});
    QVERIFY(convId > 0);

    auto conv = m_db->getConversation(convId);
    QVERIFY(conv.has_value());
    QCOMPARE(conv->type, QString("group"));
    QCOMPARE(conv->name, QString("项目群"));
    QCOMPARE(conv->memberCount, 2);

    QCOMPARE(m_db->getGroupMembers(convId).size(), 2);
    QCOMPARE(m_db->groupRole(convId, owner->id), QString("owner"));
    QCOMPARE(m_db->groupRole(convId, member->id), QString("member"));

    // 群名空白、创建者非法被拒绝
    QCOMPARE(m_db->createGroup(owner->id, "", {}), -1LL);
    QCOMPARE(m_db->createGroup(owner->id, "   ", {}), -1LL);
    QCOMPARE(m_db->createGroup(0, "群", {}), -1LL);
}

void TestDatabaseManager::addGroupMembersSkipsDuplicates()
{
    auto owner = m_db->getUserByUsername("testuser");
    auto member = m_db->getUserByUsername("user2");
    auto outsider = m_db->getUserByUsername("outsider");
    QVERIFY(owner.has_value());
    QVERIFY(member.has_value());
    QVERIFY(outsider.has_value());

    const qint64 convId = m_db->createGroup(owner->id, "邀请群", {member->id});
    QVERIFY(convId > 0);

    // 已在群中与非法 ID 被跳过，新成员加入成功
    QVERIFY(m_db->addGroupMembers(convId, {member->id, outsider->id, 0}));
    QCOMPARE(m_db->getGroupMembers(convId).size(), 3);
    QCOMPARE(m_db->groupRole(convId, outsider->id), QString("member"));

    // 全部重复的批量邀请：无变化也不报错
    QVERIFY(m_db->addGroupMembers(convId, {member->id, outsider->id}));
    QCOMPARE(m_db->getGroupMembers(convId).size(), 3);
}

void TestDatabaseManager::removeGroupMemberDeletesRow()
{
    auto owner = m_db->getUserByUsername("testuser");
    auto outsider = m_db->getUserByUsername("outsider");
    QVERIFY(owner.has_value());
    QVERIFY(outsider.has_value());

    const qint64 convId = m_db->createGroup(owner->id, "踢人群", {outsider->id});
    QVERIFY(convId > 0);

    QVERIFY(m_db->removeGroupMember(convId, outsider->id));
    QCOMPARE(m_db->groupRole(convId, outsider->id), QString());
    QCOMPARE(m_db->getGroupMembers(convId).size(), 1);

    // 重复移除返回 false
    QVERIFY(!m_db->removeGroupMember(convId, outsider->id));
}

void TestDatabaseManager::groupRoleReportsOwnershipAndNonMember()
{
    auto owner = m_db->getUserByUsername("testuser");
    auto member = m_db->getUserByUsername("user2");
    auto outsider = m_db->getUserByUsername("outsider");
    QVERIFY(owner.has_value());
    QVERIFY(member.has_value());
    QVERIFY(outsider.has_value());

    const qint64 convId = m_db->createGroup(owner->id, "角色群", {member->id});
    QVERIFY(convId > 0);

    QCOMPARE(m_db->groupRole(convId, owner->id), QString("owner"));
    QCOMPARE(m_db->groupRole(convId, member->id), QString("member"));
    QCOMPARE(m_db->groupRole(convId, outsider->id), QString());

    // 角色变更生效；非成员更新失败；非法角色取值被拒绝
    QVERIFY(m_db->updateMemberRole(convId, member->id, "admin"));
    QCOMPARE(m_db->groupRole(convId, member->id), QString("admin"));
    QVERIFY(!m_db->updateMemberRole(convId, outsider->id, "admin"));
    QVERIFY(!m_db->updateMemberRole(convId, member->id, "superadmin"));
    QCOMPARE(m_db->groupRole(convId, member->id), QString("admin"));
}

void TestDatabaseManager::getConversationsForUserIncludesGroupWithNameAndMemberCount()
{
    auto owner = m_db->getUserByUsername("testuser");
    auto member = m_db->getUserByUsername("user2");
    QVERIFY(owner.has_value());
    QVERIFY(member.has_value());

    const qint64 groupConvId = m_db->createGroup(owner->id, "列表群", {member->id});
    QVERIFY(groupConvId > 0);

    // 改群名生效
    QVERIFY(m_db->setGroupName(groupConvId, "改名后的群"));

    bool groupFound = false;
    const auto convs = m_db->getConversationsForUser(owner->id);
    for (const auto &ci : convs) {
        if (ci.id == groupConvId) {
            groupFound = true;
            QCOMPARE(ci.type, QString("group"));
            QCOMPARE(ci.name, QString("改名后的群"));
            QCOMPARE(ci.memberCount, 2);
        } else if (ci.type == "private") {
            // private 会话不受群聊字段影响
            QVERIFY(ci.name.isEmpty());
            QCOMPARE(ci.memberCount, 0);
        }
    }
    QVERIFY(groupFound);

    // setGroupName 拒绝 private 会话与空群名
    const qint64 privateConvId = m_db->getOrCreatePrivateConversation(owner->id, member->id);
    QVERIFY(privateConvId > 0);
    QVERIFY(!m_db->setGroupName(privateConvId, "不是群"));
    QVERIFY(!m_db->setGroupName(groupConvId, "  "));
}

void TestDatabaseManager::groupMemberIdsAndCounts()
{
    auto owner = m_db->getUserByUsername("testuser");
    auto member = m_db->getUserByUsername("user2");
    auto outsider = m_db->getUserByUsername("outsider");
    QVERIFY(owner.has_value());
    QVERIFY(member.has_value());
    QVERIFY(outsider.has_value());

    const qint64 convId = m_db->createGroup(owner->id, "分发群", {member->id});
    QVERIFY(convId > 0);

    // fan-out 用的成员 ID 列表
    const auto ids = m_db->getGroupMemberIds(convId);
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(owner->id));
    QVERIFY(ids.contains(member->id));

    // 回执聚合的接收者总数（排除发送方）；非成员不排除任何成员
    QCOMPARE(m_db->memberCountExcluding(convId, owner->id), 1);
    QCOMPARE(m_db->memberCountExcluding(convId, outsider->id), 2);

    // 成员移除后接收者总数同步减少
    QVERIFY(m_db->removeGroupMember(convId, member->id));
    QCOMPARE(m_db->memberCountExcluding(convId, owner->id), 0);

    // usernameById：存在返回用户名，不存在返回空串
    QCOMPARE(m_db->usernameById(owner->id), QString("testuser"));
    QCOMPARE(m_db->usernameById(999999), QString());
}

void TestDatabaseManager::groupMessageReceiptCounts()
{
    auto owner = m_db->getUserByUsername("testuser");
    auto member = m_db->getUserByUsername("user2");
    auto outsider = m_db->getUserByUsername("outsider");
    QVERIFY(owner.has_value());
    QVERIFY(member.has_value());
    QVERIFY(outsider.has_value());

    const qint64 convId = m_db->createGroup(owner->id, "回执群", {member->id, outsider->id});
    QVERIFY(convId > 0);

    const qint64 msgId = m_db->sendMessage(convId, owner->id, "hello group",
                                           "text", "grp-key-1", "devA");
    QVERIFY(msgId > 0);

    // 接收者总数 = 除发送方外全体成员
    const int recipients = m_db->memberCountExcluding(convId, owner->id);
    QCOMPARE(recipients, 2);

    // 送达：单人回执不达成，全员回执才达成
    QCOMPARE(m_db->receiptCount(msgId, "delivered"), 0);
    QVERIFY(m_db->recordMessageReceipt(msgId, member->id, "devM", "delivered"));
    QCOMPARE(m_db->receiptCount(msgId, "delivered"), 1);
    QVERIFY(m_db->recordMessageReceipt(msgId, outsider->id, "devO", "delivered"));
    QCOMPARE(m_db->receiptCount(msgId, "delivered"), recipients);

    // 已读：同样按接收者计数聚合
    QVERIFY(m_db->recordMessageReceipt(msgId, member->id, "devM", "read"));
    QCOMPARE(m_db->receiptCount(msgId, "read"), 1);
    QVERIFY(m_db->recordMessageReceipt(msgId, outsider->id, "devO", "read"));
    QCOMPARE(m_db->receiptCount(msgId, "read"), recipients);

    // 按用户去重：同一用户多设备回执不重复计数，而逐设备计数保留原语义
    QVERIFY(m_db->recordMessageReceipt(msgId, member->id, "devM2", "read"));
    QCOMPARE(m_db->receiptUserCount(msgId, "read"), recipients);
    QCOMPARE(m_db->receiptCount(msgId, "read"), recipients + 1);

    // 系统消息（contentType=system）可正常入库与读回
    const qint64 sysId = m_db->sendMessage(convId, owner->id,
                                           "{\"event\":\"group_created\"}", "system");
    QVERIFY(sysId > 0);
    auto sysMsg = m_db->getMessage(sysId);
    QVERIFY(sysMsg.has_value());
    QCOMPARE(sysMsg->contentType, QString("system"));
    QCOMPARE(sysMsg->conversationId, convId);
}

QTEST_MAIN(TestDatabaseManager)
#include "TestDatabaseManager.moc"
