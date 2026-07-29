#pragma once

#include <QObject>
#include <QTcpServer>
#include <QHash>
#include <QSet>

class RequestHandler;

class ConnectionServer : public QTcpServer
{
    Q_OBJECT

public:
    using QTcpServer::QTcpServer;

signals:
    void socketAccepted(qintptr socketDescriptor);

protected:
    void incomingConnection(qintptr socketDescriptor) override;
};

class Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);
    bool start(quint16 port);

    // 在线用户管理
    int onlineUserCount() const;
    QSet<qint64> onlineUserIds() const;

private slots:
    void onSocketAccepted(qintptr socketDescriptor);
    void onUserLoggedIn(qint64 userId, qint64 sessionId, const QString &deviceId);
    void onUserLoggedOut(qint64 userId, qint64 sessionId);
    void onHandlerFinished();
    // M3: 消息路由
    void onMessageForUser(qint64 targetUserId, const QByteArray &packetData);

private:
    ConnectionServer *tcpServer;

    // userId -> set of sessionIds
    QHash<qint64, QSet<qint64>> m_onlineSessions;
    // sessionId -> handler
    QHash<qint64, RequestHandler *> m_sessionHandlers;
    // handler -> userId (for cleanup)
    QHash<RequestHandler *, qint64> m_handlerUsers;
};
