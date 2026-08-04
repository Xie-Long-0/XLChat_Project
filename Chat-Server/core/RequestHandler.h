#pragma once

#include <QThread>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QJsonObject>
#include <QTimer>

#include <atomic>

#include "protocol/PacketCodec.h"

class DatabaseManager;
class NonceCache;

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

    // M5.5: 请求断开客户端连接（被 terminate_session 时由 Server 调用）
    void disconnectClient();

    // M5: 设置 TLS 配置（由 Server 在 start() 前调用）
    void setSslConfiguration(const QSslConfiguration &config);

    // M5.5: 设置全局 nonce 缓存（由 Server 在 start() 前调用）
    void setNonceCache(NonceCache *cache);

signals:
    void finished();
    void userLoggedIn(qint64 userId, qint64 sessionId, const QString &deviceId);
    void userLoggedOut(qint64 userId, qint64 sessionId);
    // M3: 消息路由信号
    void messageForUser(qint64 targetUserId, const QByteArray &packetData);
    // M5.5: 本人其他会话被终止，需断开对应连接
    void sessionTerminated(qint64 sessionId);

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
    void processTokenRenewRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    // M5.5: 原 force_logout 改为仅允许终止本人其他会话
    void processTerminateSessionRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);

    // M3 处理器
    void processSearchUsersRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processAddContactRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processGetContactsRequest(const XYChat::Protocol::Packet &packet);
    void processGetConversationsRequest(const XYChat::Protocol::Packet &packet);
    void processSendMessageRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processAckMessageRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    void processSyncMessagesRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);
    // M5.5: 账号级增量同步
    void processSyncEventsRequest(const XYChat::Protocol::Packet &packet, const QJsonObject &request);

    // M5.5: 重放保护（timestamp/nonce 强制必填）
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

    // M5.5: 全局 nonce 缓存（Server 持有，跨连接共享）
    NonceCache *m_nonceCache = nullptr;

    // M5.5: 发送代理对象，线程亲和于 handler 线程，
    // 避免跨线程直接访问 QSslSocket
    std::atomic<QObject *> m_sendWorker{nullptr};

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
    static constexpr int ReplayTimestampToleranceSecs = 300; // 5 分钟时间戳容差
};
