#pragma once

#include <QThread>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QJsonObject>
#include <QTimer>
#include <QSet>

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

    // M3: 供 Server 转发数据到客户端
    void sendRawData(const QByteArray &data);

    // M5: 设置 TLS 配置（由 Server 在 start() 前调用）
    void setSslConfiguration(const QSslConfiguration &config);

signals:
    void finished();
    void userLoggedIn(qint64 userId, qint64 sessionId, const QString &deviceId);
    void userLoggedOut(qint64 userId, qint64 sessionId);
    // M3: 消息路由信号
    void messageForUser(qint64 targetUserId, const QByteArray &packetData);

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

    // M3 处理器
    void processSearchUsersRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processAddContactRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processGetContactsRequest(const XYChat::Protocol::Packet &packet);
    void processGetConversationsRequest(const XYChat::Protocol::Packet &packet);
    void processSendMessageRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processAckMessageRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processSyncMessagesRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);

    // M5: 重放保护
    bool checkReplayProtection(const QJsonObject &request);

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
    QSslSocket *m_socket = nullptr;
    QTimer *m_idleTimer = nullptr;
    XYChat::Protocol::PacketCodec m_codec;
    qintptr m_socketDescriptor;

    // M5: TLS
    QSslConfiguration m_sslConfig;
    bool m_tlsEnabled = false;

    // 认证状态
    qint64 m_authenticatedUserId = 0;
    qint64 m_currentSessionId = 0;
    QString m_currentDeviceId;

    // 数据库（每个线程使用独立连接名）
    DatabaseManager *m_db = nullptr;

    // M5: 重放保护 - nonce 缓存
    QSet<QString> m_seenNonces;

    // 限流常量
    static constexpr int MaxFailedLoginsPerIP = 10;
    static constexpr int MaxFailedLoginsPerUser = 5;
    static constexpr int RateLimitWindowSeconds = 300; // 5 分钟
    static constexpr int ReplayTimestampToleranceSecs = 300; // 5 分钟时间戳容差
};
