#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

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
    void login(const QString &username, const QString &encryptedPassword);
    void registerAccount(const QString &username, const QString &password,
                         const QString &email = {}, const QString &phone = {});
    void logout();
    void renewToken();

    // 状态查询
    ConnectionState state() const { return m_state; }
    QString sessionToken() const { return m_sessionToken; }
    qint64 userId() const { return m_userId; }
    QString username() const { return m_username; }

signals:
    void loginSuccessful();
    void loginFailed(const QString &errorMessage);
    void registerSuccessful();
    void registerFailed(const QString &errorMessage);
    void logoutFinished();
    void connectionStateChanged(NetworkManager::ConnectionState state);

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
    QString m_pendingEncryptedPassword;
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
};
