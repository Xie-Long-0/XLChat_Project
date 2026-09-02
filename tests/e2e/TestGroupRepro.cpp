// 临时诊断工具：M7a 群聊端到端复现（无界面，复用 NetworkManager 真实链路）
// 场景：A 建群并拉入 B -> A 发群消息 -> B 收到并回复（“群成员发送消息”）-> A 收到
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QDebug>

#include "NetworkManager.h"

namespace
{
void logLine(const QString &line)
{
    QFile f("M7bRepro.log");
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream s(&f);
        s << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
          << " " << line << "\n";
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &,
                    const QString &msg)
{
    const char *prefix = "[Qt]";
    switch (type) {
    case QtDebugMsg: prefix = "[QtDebug]"; break;
    case QtInfoMsg: prefix = "[QtInfo]"; break;
    case QtWarningMsg: prefix = "[QtWarning]"; break;
    case QtCriticalMsg: prefix = "[QtCritical]"; break;
    case QtFatalMsg: prefix = "[QtFatal]"; break;
    }
    logLine(QString("%1 %2").arg(prefix).arg(msg));
}

void step(const QString &text)
{
    const QString line = "[Repro] " + text;
    qInfo().noquote() << line;
    logLine(line);
}

[[noreturn]] void finish(int code, const QString &text)
{
    const QString line = "[Repro] RESULT: " + text;
    qInfo().noquote() << line;
    logLine(line);
    std::exit(code);
}
} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(messageHandler);
    QCoreApplication app(argc, argv);
    app.setOrganizationName("XYChat");
    app.setApplicationName("XYChat");

    const QString suffix = QString::number(QDateTime::currentSecsSinceEpoch() % 1000000);
    const QString aliceName = "gmA_" + suffix;
    const QString bobName = "gmB_" + suffix;
    const QString password = "Repro!Pass123";

    NetworkManager alice;
    NetworkManager bob;

    qint64 bobUserId = 0;
    qint64 groupConvId = 0;
    bool bobReplied = false;
    bool phase2Armed = false;
    bool aliceLoggedIn = false;
    bool bobLoggedIn = false;
    bool searchStarted = false;

    // 全局看门狗
    QTimer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(45000);
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        finish(2, "TIMEOUT");
    });
    watchdog.start();

    // ── 注册 ──
    QObject::connect(&alice, &NetworkManager::registerSuccessful, [&]() {
        step("alice registered, logging in");
        alice.login(aliceName, password);
    });
    QObject::connect(&bob, &NetworkManager::registerSuccessful, [&]() {
        step("bob registered, logging in");
        bob.login(bobName, password);
    });
    QObject::connect(&alice, &NetworkManager::registerFailed, [&](const QString &e) {
        finish(1, "alice register failed: " + e);
    });
    QObject::connect(&bob, &NetworkManager::registerFailed, [&](const QString &e) {
        finish(1, "bob register failed: " + e);
    });

    // ── 登录 ──
    QObject::connect(&alice, &NetworkManager::loginSuccessful, [&]() {
        aliceLoggedIn = true;
        step("alice logged in, id=" + QString::number(alice.userId()));
    });
    QObject::connect(&bob, &NetworkManager::loginSuccessful, [&]() {
        bobLoggedIn = true;
        step("bob logged in, id=" + QString::number(bob.userId()));
    });
    // 双方都登录并留出 E2EE 身份/预密钥注册时间后再搜索建群
    QTimer searchPoller;
    QElapsedTimer loginElapsed;
    bool loginTimerStarted = false;
    searchPoller.setInterval(200);
    QObject::connect(&searchPoller, &QTimer::timeout, [&]() {
        if (!aliceLoggedIn || !bobLoggedIn) {
            return;
        }
        if (!loginTimerStarted) {
            loginTimerStarted = true;
            loginElapsed.start();
            return;
        }
        if (!searchStarted && loginElapsed.elapsed() > 2000) {
            searchStarted = true;
            alice.searchUsers(bobName);
        }
    });
    searchPoller.start();
    QObject::connect(&alice, &NetworkManager::loginFailed, [&](const QString &e) {
        finish(1, "alice login failed: " + e);
    });
    QObject::connect(&bob, &NetworkManager::loginFailed, [&](const QString &e) {
        finish(1, "bob login failed: " + e);
    });

    // ── 搜索结果 -> 建群 ──
    QObject::connect(&alice, &NetworkManager::searchUsersResult, [&](const QJsonArray &users) {
        for (const QJsonValue &v : users) {
            const QJsonObject u = v.toObject();
            if (u.value("username").toString() == bobName) {
                bobUserId = u.value("userId").toVariant().toLongLong();
            }
        }
        if (bobUserId <= 0) {
            finish(1, "bob not found in search results");
        }
        step("alice creating group with member " + QString::number(bobUserId));
        alice.createGroup("ReproGroup", QVariantList{bobUserId});
    });

    // ── 建群成功 -> 群主发第一条消息 ──
    QObject::connect(&alice, &NetworkManager::groupCreated, [&](qint64 convId, const QString &name) {
        groupConvId = convId;
        step("group created id=" + QString::number(convId) + " name=" + name);
        alice.getConversations();
        const QString cmid = alice.sendGroupMessage(convId, "owner hello");
        step("alice sent group message, cmid=" + cmid);
    });
    QObject::connect(&alice, &NetworkManager::groupRequestFailed, [&](const QString &e) {
        finish(1, "alice group request failed: " + e);
    });

    // ── alice 发送确认 ──
    QObject::connect(&alice, &NetworkManager::messageSent,
                     [&](qint64 messageId, qint64 conversationId, const QString &cmid) {
        step("alice messageSent id=" + QString::number(messageId)
             + " conv=" + QString::number(conversationId));
        Q_UNUSED(cmid);
    });
    QObject::connect(&alice, &NetworkManager::messageSendFailed, [&](const QString &e) {
        finish(1, "alice send failed: " + e);
    });

    // ── bob 收到群消息 -> 确认送达并回复（“群成员发送消息”崩溃点） ──
    QObject::connect(&bob, &NetworkManager::newMessageReceived, [&](const QJsonObject &msg) {
        const qint64 msgId = msg.value("messageId").toVariant().toLongLong();
        const qint64 convId = msg.value("conversationId").toVariant().toLongLong();
        const QString contentType = msg.value("contentType").toString();
        step("bob received message id=" + QString::number(msgId)
             + " type=" + contentType
             + " content=" + msg.value("content").toString());
        bob.ackMessage(msgId, "delivered");
        // 系统消息不触发回复；等待收到群主的第一条 E2EE 消息后再回复
        if (contentType == "system") {
            return;
        }
        if (!bobReplied) {
            bobReplied = true;
            const QString cmid = bob.sendGroupMessage(convId, "member hello");
            step("bob sent group message, cmid=" + cmid);
        }
    });

    // ── alice 收到 bob 的回复 -> 阶段二：双方登出再重登，群成员再发消息 ──
    QObject::connect(&alice, &NetworkManager::newMessageReceived, [&](const QJsonObject &msg) {
        const QString content = msg.value("content").toString();
        step("alice received message content=" + content);
        alice.ackMessage(msg.value("messageId").toVariant().toLongLong(), "read");
        if (content == "member hello") {
            step("roundtrip OK; phase2: logout both then re-login");
            alice.logout();
            bob.logout();
        } else if (content == "member msg 2") {
            finish(0, "SUCCESS: phase2 group message after re-login OK");
        }
    });

    QObject::connect(&alice, &NetworkManager::logoutFinished, [&]() {
        step("alice logged out, logging in again");
        phase2Armed = true;
        alice.login(aliceName, password);
    });
    QObject::connect(&bob, &NetworkManager::logoutFinished, [&]() {
        step("bob logged out, logging in again");
        bob.login(bobName, password);
    });

    // 阶段二：重登完成后（双方均 Authenticated）由 bob 再发一条群消息
    QTimer phase2Poller;
    phase2Poller.setInterval(300);
    bool phase2Started = false;
    QObject::connect(&phase2Poller, &QTimer::timeout, [&]() {
        if (phase2Started) {
            return;
        }
        const bool bothAuthed =
            alice.state() == NetworkManager::ConnectionState::Authenticated
            && bob.state() == NetworkManager::ConnectionState::Authenticated;
        if (phase2Armed && bothAuthed && groupConvId > 0) {
            phase2Started = true;
            const QString cmid = bob.sendGroupMessage(groupConvId, "member msg 2");
            step("phase2: bob sent group message after re-login, cmid=" + cmid);
        }
    });
    phase2Poller.start();

    QObject::connect(&bob, &NetworkManager::messageSendFailed, [&](const QString &e) {
        finish(1, "bob send failed: " + e);
    });

    step("registering users " + aliceName + " / " + bobName);
    alice.registerAccount(aliceName, password);
    bob.registerAccount(bobName, password);

    return app.exec();
}
