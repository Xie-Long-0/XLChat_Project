#pragma once

#include <QObject>
#include <QTcpServer>
#include <QSslConfiguration>
#include <QHash>
#include <QSet>

#include "NonceCache.h"

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
    // M5.5: fail-closed：若 TLS 未启用且未显式允许明文，start() 拒绝启动
    bool start(quint16 port, bool allowPlaintext = false);

    // M5: TLS 配置
    bool initTls(const QString &certDir);

    // M5.5: 是否处于 TLS 保护状态（供测试与监控）
    bool tlsEnabled() const { return m_tlsEnabled; }

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
    // M5.5: 本人会话被终止时断开对应连接
    void onSessionTerminated(qint64 sessionId);

private:
    ConnectionServer *tcpServer;

    // M5: TLS 配置
    QSslConfiguration m_sslConfig;
    bool m_tlsEnabled = false;

    // M5.5: 全局 nonce 缓存（跨连接共享，TTL 去重）
    NonceCache m_nonceCache;

    // userId -> set of sessionIds
    QHash<qint64, QSet<qint64>> m_onlineSessions;
    // sessionId -> handler
    QHash<qint64, RequestHandler *> m_sessionHandlers;
    // handler -> userId (for cleanup)
    QHash<RequestHandler *, qint64> m_handlerUsers;
};
