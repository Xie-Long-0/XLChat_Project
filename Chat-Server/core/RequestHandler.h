#pragma once

#include <QThread>
#include <QTcpSocket>
#include <QJsonObject>
#include <QTimer>

#include "protocol/PacketCodec.h"

class DatabaseManager;

class RequestHandler : public QThread
{
    Q_OBJECT

public:
    explicit RequestHandler(qintptr socketDescriptor, QObject *parent = nullptr);
    ~RequestHandler() override;

    // 供 Server 查询当前认证用户
    qint64 authenticatedUserId() const { return m_authenticatedUserId; }
    qint64 currentSessionId() const { return m_currentSessionId; }

signals:
    void finished();
    void userLoggedIn(qint64 userId, qint64 sessionId, const QString &deviceId);
    void userLoggedOut(qint64 userId, qint64 sessionId);

private slots:
    void onReadyRead();
    void onIdleTimeout();

private:
    void run() override;

    void processPacket(const XYChat::Protocol::Packet &packet);

    // 各请求处理器
    void processLoginRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processRegisterRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processLogoutRequest(const XYChat::Protocol::Packet &packet);
    void processTokenRenewRequest(const XYChat::Protocol::Packet &packet);
    void processForceLogoutRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);

    // 工具方法
    void sendResponse(quint64 requestId,
                      XYChat::Protocol::MessageType messageType,
                      XYChat::Protocol::ErrorCode code,
                      const QString &message,
                      const QJsonObject &data = {});
    void sendPacket(const XYChat::Protocol::Packet &packet);
    bool validateSession();
    bool checkRateLimit(const QString &ipAddress, qint64 userId);

private:
    QTcpSocket *m_socket = nullptr;
    QTimer *m_idleTimer = nullptr;
    XYChat::Protocol::PacketCodec m_codec;
    qintptr m_socketDescriptor;

    // 认证状态
    qint64 m_authenticatedUserId = 0;
    qint64 m_currentSessionId = 0;
    QString m_currentDeviceId;

    // 数据库（每个线程使用独立连接名）
    DatabaseManager *m_db = nullptr;

    // 限流常量
    static constexpr int MaxFailedLoginsPerIP = 10;
    static constexpr int MaxFailedLoginsPerUser = 5;
    static constexpr int RateLimitWindowSeconds = 300; // 5 分钟
};
