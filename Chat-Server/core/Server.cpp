#include "Server.h"
#include "RequestHandler.h"

void ConnectionServer::incomingConnection(qintptr socketDescriptor)
{
    emit socketAccepted(socketDescriptor);
}

Server::Server(QObject *parent)
    : QObject(parent)
    , tcpServer(new ConnectionServer(this))
{
    connect(tcpServer, &ConnectionServer::socketAccepted, this, &Server::onSocketAccepted);
}

bool Server::start(quint16 port)
{
    if (tcpServer->listen(QHostAddress::Any, port)) {
        qDebug() << "[Server] Listening on port" << port;
        return true;
    }
    qDebug() << "[Server] Failed to listen on port" << port << tcpServer->errorString();
    return false;
}

void Server::onSocketAccepted(qintptr socketDescriptor)
{
    RequestHandler *handler = new RequestHandler(socketDescriptor, this);

    connect(handler, &RequestHandler::userLoggedIn, this, &Server::onUserLoggedIn);
    connect(handler, &RequestHandler::userLoggedOut, this, &Server::onUserLoggedOut);
    connect(handler, &RequestHandler::finished, this, &Server::onHandlerFinished);
    connect(handler, &RequestHandler::finished, handler, &RequestHandler::deleteLater);
    // M3: 消息路由
    connect(handler, &RequestHandler::messageForUser, this, &Server::onMessageForUser);

    handler->start();
}

void Server::onUserLoggedIn(qint64 userId, qint64 sessionId, const QString &deviceId)
{
    Q_UNUSED(deviceId);
    RequestHandler *handler = qobject_cast<RequestHandler *>(sender());
    if (!handler) return;

    m_onlineSessions[userId].insert(sessionId);
    m_sessionHandlers[sessionId] = handler;
    m_handlerUsers[handler] = userId;

    qDebug() << "[Server] User" << userId << "online. Session:" << sessionId
             << "Total online:" << onlineUserCount();
}

void Server::onUserLoggedOut(qint64 userId, qint64 sessionId)
{
    if (sessionId == 0) {
        // 强制下线：清除该用户所有 session
        m_onlineSessions.remove(userId);
        auto it = m_handlerUsers.begin();
        while (it != m_handlerUsers.end()) {
            if (it.value() == userId) {
                it = m_handlerUsers.erase(it);
            } else {
                ++it;
            }
        }
        qDebug() << "[Server] User" << userId << "force logged out. All sessions cleared.";
    } else {
        m_onlineSessions[userId].remove(sessionId);
        if (m_onlineSessions[userId].isEmpty()) {
            m_onlineSessions.remove(userId);
        }
        m_sessionHandlers.remove(sessionId);
        qDebug() << "[Server] User" << userId << "session" << sessionId << "ended.";
    }
}

void Server::onHandlerFinished()
{
    RequestHandler *handler = qobject_cast<RequestHandler *>(sender());
    if (!handler) return;

    auto it = m_handlerUsers.find(handler);
    if (it != m_handlerUsers.end()) {
        const qint64 userId = it.value();
        m_handlerUsers.erase(it);

        if (m_onlineSessions.contains(userId)) {
            auto sit = m_sessionHandlers.begin();
            while (sit != m_sessionHandlers.end()) {
                if (sit.value() == handler) {
                    m_onlineSessions[userId].remove(sit.key());
                    sit = m_sessionHandlers.erase(sit);
                } else {
                    ++sit;
                }
            }
            if (m_onlineSessions[userId].isEmpty()) {
                m_onlineSessions.remove(userId);
            }
        }
    }

    qDebug() << "[Server] Handler finished. Total online users:" << onlineUserCount();
}

int Server::onlineUserCount() const
{
    return m_onlineSessions.size();
}

QSet<qint64> Server::onlineUserIds() const
{
    QSet<qint64> result;
    for (auto it = m_onlineSessions.begin(); it != m_onlineSessions.end(); ++it) {
        result.insert(it.key());
    }
    return result;
}

// ── M3: 消息路由 ───────────────────────────────────────────────────────────
void Server::onMessageForUser(qint64 targetUserId, const QByteArray &packetData)
{
    // 查找目标用户的所有在线 handler
    auto it = m_onlineSessions.find(targetUserId);
    if (it == m_onlineSessions.end()) {
        // 用户不在线，消息已存储在数据库中，用户上线后可通过 sync 获取
        return;
    }

    const QSet<qint64> sessionIds = it.value();
    for (qint64 sessionId : sessionIds) {
        auto handlerIt = m_sessionHandlers.find(sessionId);
        if (handlerIt != m_sessionHandlers.end()) {
            handlerIt.value()->sendRawData(packetData);
        }
    }
}
