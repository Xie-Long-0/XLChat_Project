/**
 * M6.5: LocalStore 本地加密持久化缓存单元测试
 *
 * 覆盖：schema/游标持久化、持久化 outbox、消息与会话缓存、
 *       磁盘密文不可读（fail-closed）、解密缓存归口与遗留迁移、
 *       登出销毁（closeAndDestroy）。
 */
#include <QtTest>
#include <QFile>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

#include "KeyStorage.h"
#include "LocalStore.h"
#include "encryption/E2eeCrypto.h"

namespace
{
const QString DeviceId = "testdevice";

QString uniqueUser()
{
    return "user_"
        + QUuid::createUuid().toString(QUuid::Id128).left(12).toLower();
}

QJsonObject makeMessage(qint64 messageId, qint64 conversationId, const QString &content,
                        qint64 senderId = 2, const QString &senderUsername = "bob",
                        const QString &status = "delivered")
{
    QJsonObject msg;
    msg["messageId"] = messageId;
    msg["conversationId"] = conversationId;
    msg["senderId"] = senderId;
    msg["senderUsername"] = senderUsername;
    msg["content"] = content;
    msg["contentType"] = "text";
    msg["status"] = status;
    msg["createdAt"] = "2026-08-21T00:00:00Z";
    return msg;
}

QByteArray readRawDb(const QString &user)
{
    QFile file(LocalStore::dbFilePath(user, DeviceId));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}
} // namespace

class TestLocalStore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("XYChat");
        QCoreApplication::setApplicationName("XYChatTestLocalStore");
        // 重定向 QStandardPaths 到测试目录，避免污染真实 AppData
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(!QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).isEmpty());
    }

    // schema 与同步游标
    void schemaAndSyncCursorPersistAcrossReopen()
    {
        const QString user = uniqueUser();
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QCOMPARE(store.syncCursor(), 0);
        QVERIFY(store.setSyncCursor(42));
        store.close();

        // 模拟应用重启：重新打开后游标仍在
        QVERIFY(store.open(user, DeviceId));
        QCOMPARE(store.syncCursor(), 42);
        store.closeAndDestroy();
    }

    // 持久化 outbox
    void outboxPersistsAcrossReopen()
    {
        const QString user = uniqueUser();
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QVERIFY(store.addOutboxItem("cmid-1", 7, "outbox plain 1"));
        QVERIFY(store.addOutboxItem("cmid-2", 8, "outbox plain 2"));
        store.close();

        // 重启后 outbox 完整恢复（含解密后的明文）
        QVERIFY(store.open(user, DeviceId));
        const auto items = store.loadOutbox();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).clientMessageId, "cmid-1");
        QCOMPARE(items.at(0).toUserId, 7LL);
        QCOMPARE(items.at(0).content, "outbox plain 1");

        // 确认后删除（幂等键）
        QVERIFY(store.removeOutboxItem("cmid-1"));
        QCOMPARE(store.loadOutbox().size(), 1);
        store.closeAndDestroy();
    }

    void outboxContentEncryptedOnDisk()
    {
        const QString user = uniqueUser();
        const QString secret = "OutboxTopSecret-M6.5-Probe";
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QVERIFY(store.addOutboxItem("cmid-x", 9, secret));
        store.close();

        // fail-closed 验收：磁盘文件中不存在可读明文
        const QByteArray raw = readRawDb(user);
        QVERIFY(!raw.isEmpty());
        QVERIFY(!raw.contains(secret.toUtf8()));

        QVERIFY(store.open(user, DeviceId));
        QCOMPARE(store.loadOutbox().size(), 1);
        store.closeAndDestroy();
    }

    // 消息缓存
    void messageContentEncryptedOnDisk()
    {
        const QString user = uniqueUser();
        const QString secret = "MessageTopSecret-M6.5-Probe";
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QVERIFY(store.upsertMessage(makeMessage(1, 10, secret)));
        store.close();

        const QByteArray raw = readRawDb(user);
        QVERIFY(!raw.isEmpty());
        QVERIFY(!raw.contains(secret.toUtf8()));

        // 重启后仍可读出解密正文
        QVERIFY(store.open(user, DeviceId));
        const QJsonArray messages = store.loadMessages(10);
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.at(0).toObject().value("content").toString(), secret);
        store.closeAndDestroy();
    }

    void loadMessagesOrderedAndStatusUpdates()
    {
        const QString user = uniqueUser();
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        // 乱序写入，读取需按 messageId 升序
        QVERIFY(store.upsertMessage(makeMessage(3, 11, "third")));
        QVERIFY(store.upsertMessage(makeMessage(1, 11, "first")));
        QVERIFY(store.upsertMessage(makeMessage(2, 11, "second")));
        QVERIFY(store.upsertMessage(makeMessage(99, 12, "other conv")));

        const QJsonArray messages = store.loadMessages(11);
        QCOMPARE(messages.size(), 3);
        QCOMPARE(messages.at(0).toObject().value("messageId").toVariant().toLongLong(), 1LL);
        QCOMPARE(messages.at(1).toObject().value("messageId").toVariant().toLongLong(), 2LL);
        QCOMPARE(messages.at(2).toObject().value("messageId").toVariant().toLongLong(), 3LL);

        // 状态更新（已送达/已读推送落缓存）
        QVERIFY(store.updateMessageStatus(1, "read"));
        const QJsonArray updated = store.loadMessages(11);
        QCOMPARE(updated.at(0).toObject().value("status").toString(), "read");
        store.closeAndDestroy();
    }

    void upsertKeepsDecryptedOverUndecryptable()
    {
        const QString user = uniqueUser();
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QVERIFY(store.upsertMessage(makeMessage(5, 20, "decrypted plain")));

        // 重新同步到同一消息但暂不可解密：不得覆盖既有明文
        QJsonObject undecryptable = makeMessage(5, 20, QString());
        undecryptable["undecryptable"] = true;
        QVERIFY(store.upsertMessage(undecryptable));
        QJsonArray messages = store.loadMessages(20);
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.at(0).toObject().value("content").toString(),
                 "decrypted plain");
        QVERIFY(!messages.at(0).toObject().contains("undecryptable"));

        // 新的可解密正文允许覆盖
        QVERIFY(store.upsertMessage(makeMessage(5, 20, "re-decrypted")));
        messages = store.loadMessages(20);
        QCOMPARE(messages.at(0).toObject().value("content").toString(),
                 "re-decrypted");
        store.closeAndDestroy();
    }

    // 登出清理语义（运行期缺陷回归）
    void logoutClearsUserDataButKeepsDecryptCache()
    {
        const QString user = uniqueUser();
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QVERIFY(store.upsertMessage(makeMessage(1, 50, "visible message")));
        QVERIFY(store.addOutboxItem("cmid-l", 7, "pending"));
        QVERIFY(store.setSyncCursor(15));
        QVERIFY(store.saveDecryptedContent(1, "visible message"));

        // 登出语义：用户可见数据全部清除
        QVERIFY(store.clearUserData());
        QCOMPARE(store.loadMessages(50).size(), 0);
        QCOMPARE(store.loadOutbox().size(), 0);
        QCOMPARE(store.syncCursor(), 0);
        QCOMPARE(store.loadConversations().size(), 0);

        // 但解密缓存（E2EE 密钥材料）必须保留：预密钥已消费不可恢复，
        // 登出重登后对方消息只能靠它兜底（此前整库销毁导致重登无法解密）
        QCOMPARE(store.loadDecryptedContent(1), "visible message");
        store.close();

        // 重新登录（重开库）后仍在
        QVERIFY(store.open(user, DeviceId));
        QCOMPARE(store.loadDecryptedContent(1), "visible message");
        store.closeAndDestroy();
    }

    void upsertNeverPersistsEnvelopeCiphertext()
    {
        const QString user = uniqueUser();
        const QString envelope = "{\"v\":1,\"devices\":[]}";
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));

        // 解密失败路径：undecryptable 标志 + content 残留 envelope 原文
        QJsonObject failed = makeMessage(1, 60, envelope);
        failed["undecryptable"] = true;
        QVERIFY(store.upsertMessage(failed));
        // 防御分支：无标志但 content 本身就是 envelope
        QVERIFY(store.upsertMessage(makeMessage(2, 60, envelope)));

        const QJsonArray messages = store.loadMessages(60);
        QCOMPARE(messages.size(), 2);
        for (const QJsonValue &value : messages) {
            const QJsonObject msg = value.toObject();
            QVERIFY2(msg.value("content").toString().isEmpty(),
                     "envelope ciphertext must never be cached as content");
            QVERIFY(msg.value("undecryptable").toBool());
        }
        store.closeAndDestroy();
    }

    void healsLegacyEnvelopeLeakRows()
    {
        const QString user = uniqueUser();
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QVERIFY(store.upsertMessage(makeMessage(1, 70, "healthy row")));
        store.close();

        // 模拟旧缺陷版本写入的污染行：envelope 原文以存储密钥加密落库
        const QByteArray key = KeyStorage::loadLocalStoreKey(user, DeviceId);
        QCOMPARE(key.size(), 32);
        const QString envelope = "{\"v\":1,\"devices\":[]}";
        const auto gcm = XYChat::Security::E2eeCrypto::aesGcmEncrypt(key, envelope.toUtf8());
        QVERIFY(gcm.valid);
        const QString enc = "enc1:"
            + QString::fromLatin1(gcm.iv.toBase64()) + QLatin1Char(':')
            + QString::fromLatin1(gcm.ciphertext.toBase64());
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE",
                                                        "healprobe");
            db.setDatabaseName(LocalStore::dbFilePath(user, DeviceId));
            QVERIFY(db.open());
            QSqlQuery query(db);
            query.prepare(
                "UPDATE messages SET content_enc = ?, undecryptable = 0 WHERE message_id = 1");
            query.addBindValue(enc);
            QVERIFY(query.exec());
            db.close();
        }
        QSqlDatabase::removeDatabase("healprobe");

        // 再次打开触发自愈：污染行被清空为 undecryptable，不再泄漏到 UI
        QVERIFY(store.open(user, DeviceId));
        const QJsonArray messages = store.loadMessages(70);
        QCOMPARE(messages.size(), 1);
        QVERIFY(messages.at(0).toObject().value("content").toString().isEmpty());
        QVERIFY(messages.at(0).toObject().value("undecryptable").toBool());
        store.closeAndDestroy();
    }

    // 回执状态只前进不回退
    void statusOnlyMovesForward()
    {
        const QString user = uniqueUser();
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QVERIFY(store.upsertMessage(makeMessage(1, 30, "m", 2, "bob", "delivered")));
        // 回执推送推进到已读
        QVERIFY(store.updateMessageStatus(1, "read"));

        // 滞后同步不得把 read 回退为 delivered（审查修复回归项）
        QVERIFY(store.upsertMessage(makeMessage(1, 30, "m", 2, "bob", "delivered")));
        QJsonArray messages = store.loadMessages(30);
        QCOMPARE(messages.at(0).toObject().value("status").toString(),
                 "read");

        // 前进方向覆盖允许（内容同步更新）
        QVERIFY(store.upsertMessage(makeMessage(1, 30, "m2", 2, "bob", "read")));
        messages = store.loadMessages(30);
        QCOMPARE(messages.at(0).toObject().value("status").toString(),
                 "read");
        QCOMPARE(messages.at(0).toObject().value("content").toString(),
                 "m2");
        store.closeAndDestroy();
    }

    // 会话缓存
    void conversationRoundTripAndPreviewBump()
    {
        const QString user = uniqueUser();
        const QString secretPreview = "SecretPreview-M6.5-Probe";
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));

        QJsonObject conv;
        conv["conversationId"] = 5;
        conv["type"] = "private";
        conv["peerUserId"] = 7;
        conv["peerUsername"] = "alice";
        conv["lastMessage"] = secretPreview;
        conv["lastMessageId"] = 3;
        conv["lastMessageAt"] = "2026-08-21T00:00:00Z";
        conv["unreadCount"] = 2;
        QVERIFY(store.upsertConversation(conv));
        store.close();

        // 预览同样加密落库
        const QByteArray raw = readRawDb(user);
        QVERIFY(!raw.isEmpty());
        QVERIFY(!raw.contains(secretPreview.toUtf8()));

        QVERIFY(store.open(user, DeviceId));
        QJsonArray conversations = store.loadConversations();
        QCOMPARE(conversations.size(), 1);
        const QJsonObject loaded = conversations.at(0).toObject();
        QCOMPARE(loaded.value("conversationId").toVariant().toLongLong(), 5LL);
        QCOMPARE(loaded.value("peerUsername").toString(), "alice");
        QCOMPARE(loaded.value("lastMessage").toString(), secretPreview);
        QCOMPARE(loaded.value("unreadCount").toInt(), 2);

        // 新消息到达：预览与未读数更新
        QVERIFY(store.bumpConversationPreview(5, "new preview", true));
        conversations = store.loadConversations();
        QCOMPARE(conversations.at(0).toObject().value("lastMessage").toString(),
                 "new preview");
        QCOMPARE(conversations.at(0).toObject().value("unreadCount").toInt(), 3);

        // 不存在的会话不产生幻影行（事件流缺会话元数据时的保护）
        QVERIFY(store.bumpConversationPreview(99, "x", true));
        QCOMPARE(store.loadConversations().size(), 1);
        store.closeAndDestroy();
    }

    // 解密缓存归口与遗留迁移
    void decryptCacheLegacyImport()
    {
        const QString user = uniqueUser();
        QHash<qint64, QString> legacy;
        legacy.insert(101, "legacy-plain-1");
        legacy.insert(102, "legacy-plain-2");
        QVERIFY(KeyStorage::saveDecryptCache(user, DeviceId, legacy));

        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QCOMPARE(store.importLegacyDecryptCache(user, DeviceId), 2);
        QCOMPARE(store.loadDecryptedContent(101), "legacy-plain-1");
        QCOMPARE(store.loadDecryptedContent(102), "legacy-plain-2");

        // 遗留文件已删除（二次导入为空操作）
        QVERIFY(KeyStorage::loadDecryptCache(user, DeviceId).isEmpty());
        QCOMPARE(store.importLegacyDecryptCache(user, DeviceId), 0);

        // 新缓存写入/读取
        QVERIFY(store.saveDecryptedContent(103, "fresh-plain"));
        QCOMPARE(store.loadDecryptedContent(103), "fresh-plain");
        store.closeAndDestroy();
    }

    // 登出销毁
    void destroyRemovesLocalData()
    {
        const QString user = uniqueUser();
        LocalStore store;
        QVERIFY(store.open(user, DeviceId));
        QVERIFY(store.upsertMessage(makeMessage(1, 10, "to be destroyed")));
        QVERIFY(store.addOutboxItem("cmid-d", 7, "gone"));
        QVERIFY(store.setSyncCursor(7));
        store.closeAndDestroy();

        // 数据库文件与存储密钥均被删除，旧密文不可再恢复
        QVERIFY(!QFile::exists(LocalStore::dbFilePath(user, DeviceId)));
        QVERIFY(KeyStorage::loadLocalStoreKey(user, DeviceId).isEmpty());

        // 重新登录（再次 open）得到全新空库
        QVERIFY(store.open(user, DeviceId));
        QCOMPARE(store.syncCursor(), 0);
        QCOMPARE(store.loadMessages(10).size(), 0);
        QCOMPARE(store.loadOutbox().size(), 0);
        store.closeAndDestroy();
    }
};

QTEST_GUILESS_MAIN(TestLocalStore)
#include "TestLocalStore.moc"
