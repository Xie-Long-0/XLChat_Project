#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>

#include "protocol/PacketCodec.h"

class NetworkManager : public QObject
{
    Q_OBJECT

public:
    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Connected,
        LoggingIn,
        Authenticated
    };

    explicit NetworkManager(QObject *parent = nullptr);

    // 认证操作
    Q_INVOKABLE void login(const QString &username, const QString &password);
    Q_INVOKABLE void registerAccount(const QString &username, const QString &password,
                                     const QString &email = {}, const QString &phone = {});
    Q_INVOKABLE void logout();
    Q_INVOKABLE void renewToken();

    // M3: 用户搜索与联系人
    Q_INVOKABLE void searchUsers(const QString &query);
    Q_INVOKABLE void addContact(qint64 userId);
    Q_INVOKABLE void getContacts();

    // M3: 会话与消息
    Q_INVOKABLE void getConversations();
    Q_INVOKABLE void sendMessage(qint64 toUserId, const QString &content);
    Q_INVOKABLE void ackMessage(qint64 messageId, const QString &status = "delivered");
    Q_INVOKABLE void syncMessages(qint64 conversationId, qint64 afterId = 0, int limit = 100);

    // 状态查询
    ConnectionState state() const { return m_state; }
    QString sessionToken() const { return m_sessionToken; }
    qint64 userId() const { return m_userId; }
    QString username() const { return m_username; }

    // QML 可调用的方法
    Q_INVOKABLE QString encryptPassword(const QString &password) const;
    Q_INVOKABLE QVariantList toVariantList(const QJsonArray &array) const;

signals:
    void loginSuccessful();
    void loginFailed(const QString &errorMessage);
    void registerSuccessful();
    void registerFailed(const QString &errorMessage);
    void logoutFinished();
    void connectionStateChanged(NetworkManager::ConnectionState state);
    // M3 信号
    void searchUsersResult(const QJsonArray &users);
    void contactsResult(const QJsonArray &contacts);
    void conversationsResult(const QJsonArray &conversations);
    void messageSent(qint64 messageId, qint64 conversationId);
    void messageSendFailed(const QString &error);
    void newMessageReceived(const QJsonObject &message);
    void messagesSynced(qint64 conversationId, const QJsonArray &messages, bool hasMore);
    void messageAcked(qint64 messageId);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError socketError);
    void sendHeartbeat();

private:
    void connectToServer();
    void sendLoginRequest();
    void sendRegisterRequest();
    void handlePacket(const XYChat::Protocol::Packet &packet);
    void handleLoginResponse(const XYChat::Protocol::Packet &packet);
    void handleRegisterResponse(const XYChat::Protocol::Packet &packet);
    void handleLogoutResponse(const XYChat::Protocol::Packet &packet);
    void handleTokenRenewResponse(const XYChat::Protocol::Packet &packet);
    // M3 响应处理
    void handleSearchUsersResponse(const XYChat::Protocol::Packet &packet);
    void handleAddContactResponse(const XYChat::Protocol::Packet &packet);
    void handleGetContactsResponse(const XYChat::Protocol::Packet &packet);
    void handleGetConversationsResponse(const XYChat::Protocol::Packet &packet);
    void handleSendMessageResponse(const XYChat::Protocol::Packet &packet);
    void handleAckMessageResponse(const XYChat::Protocol::Packet &packet);
    void handleSyncMessagesResponse(const XYChat::Protocol::Packet &packet);
    void handleNewMessageNotification(const XYChat::Protocol::Packet &packet);
    void sendPacket(const XYChat::Protocol::Packet &packet);
    quint64 nextRequestId();
    void setState(ConnectionState state);
    void resetAuthState();

private:
    QTcpSocket *m_tcpSocket;
    QTimer *m_heartbeatTimer;
    QTimer *m_reconnectTimer;
    XYChat::Protocol::PacketCodec m_codec;
    ConnectionState m_state = ConnectionState::Disconnected;
    quint64 m_nextRequestId = 1;

    // 登录/注册待处理
    quint64 m_pendingLoginRequestId = 0;
    quint64 m_pendingRegisterRequestId = 0;
    QString m_pendingUsername;
    QString m_pendingPassword;
    QString m_pendingRegisterPassword;
    QString m_pendingEmail;
    QString m_pendingPhone;
    bool m_loginQueued = false;
    bool m_registerQueued = false;
    bool m_reconnectEnabled = false;

    // 认证后状态
    QString m_sessionToken;
    qint64 m_userId = 0;
    QString m_username;

    // M3: 待处理请求 ID
    quint64 m_pendingSearchRequestId = 0;
    quint64 m_pendingAddContactRequestId = 0;
    quint64 m_pendingGetContactsRequestId = 0;
    quint64 m_pendingGetConversationsRequestId = 0;
    quint64 m_pendingSendMessageRequestId = 0;
    quint64 m_pendingAckMessageRequestId = 0;
    quint64 m_pendingSyncMessagesRequestId = 0;
};
