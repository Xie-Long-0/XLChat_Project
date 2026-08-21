#include "Server.h"
#include "RequestHandler.h"
#include "TlsHelper.h"

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

bool Server::initTls(const QString &certDir)
{
    using namespace XYChat::Security;

    const QString certPath = certDir + "/server.crt";
    const QString keyPath = certDir + "/server.key";
    const QString caCertPath = certDir + "/ca.crt";

    TlsHelper::TlsConfig tlsConfig = TlsHelper::loadServerConfig(
        certPath, keyPath, caCertPath, true /* allowGenerate */);

    if (!tlsConfig.valid) {
        qCritical() << "[Server] TLS initialization failed";
        return false;
    }

    m_sslConfig = QSslConfiguration::defaultConfiguration();
    m_sslConfig.setLocalCertificate(tlsConfig.certificate);
    m_sslConfig.setPrivateKey(tlsConfig.privateKey);
    m_sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    m_tlsEnabled = true;

    qInfo() << "[Server] TLS enabled (TLS 1.2+)";
    return true;
}

bool Server::start(quint16 port, bool allowPlaintext)
{
    // M5.5: fail-closed：TLS 不可用时拒绝启动，
    // 除非显式开启开发明文模式（默认关闭）
    if (!m_tlsEnabled && !allowPlaintext) {
        qCritical() << "[Server] TLS is not available; refusing to start in plaintext."
                    << "Fix TLS configuration or pass --allow-plaintext for development only.";
        return false;
    }
    if (!m_tlsEnabled && allowPlaintext) {
        qWarning() << "[Server] Starting in PLAINTEXT development mode. Do not use in production.";
    }

    if (tcpServer->listen(QHostAddress::Any, port)) {
        qDebug() << "[Server] Listening on port" << port
                 << (m_tlsEnabled ? "(TLS)" : "(plain TCP, dev mode)");
        return true;
    }
    qDebug() << "[Server] Failed to listen on port" << port << tcpServer->errorString();
    return false;
}

void Server::onSocketAccepted(qintptr socketDescriptor)
{
    RequestHandler *handler = new RequestHandler(socketDescriptor, this);

    // M5: 传递 TLS 配置
    if (m_tlsEnabled) {
        handler->setSslConfiguration(m_sslConfig);
    }
    // M5.5: 传递全局 nonce 缓存
    handler->setNonceCache(&m_nonceCache);

    connect(handler, &RequestHandler::userLoggedIn, this, &Server::onUserLoggedIn);
    connect(handler, &RequestHandler::userLoggedOut, this, &Server::onUserLoggedOut);
    connect(handler, &RequestHandler::finished, this, &Server::onHandlerFinished);
    connect(handler, &RequestHandler::finished, handler, &RequestHandler::deleteLater);
    // M3: 消息路由
    connect(handler, &RequestHandler::messageForUser, this, &Server::onMessageForUser);
    // M5.5: 会话终止路由
    connect(handler, &RequestHandler::sessionTerminated, this, &Server::onSessionTerminated);

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

// M3: 消息路由
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

// M5.5: 本人其他会话被 terminate_session 终止时，断开对应连接
void Server::onSessionTerminated(qint64 sessionId)
{
    auto handlerIt = m_sessionHandlers.find(sessionId);
    if (handlerIt != m_sessionHandlers.end()) {
        handlerIt.value()->disconnectClient();
        qDebug() << "[Server] Session" << sessionId << "terminated, disconnecting client.";
    }
}
