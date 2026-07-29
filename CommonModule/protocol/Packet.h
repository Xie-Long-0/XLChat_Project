#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace XYChat::Protocol
{
inline constexpr quint32 Magic = 0x58594350; // "XYCP"
inline constexpr quint16 CurrentVersion = 1;
inline constexpr quint32 HeaderSize = sizeof(quint32) + sizeof(quint16) + sizeof(quint16)
    + sizeof(quint64) + sizeof(quint32);
inline constexpr quint32 MaxPayloadSize = 4 * 1024 * 1024;

enum class MessageType : quint16
{
    // M1
    LoginRequest = 1,
    LoginResponse = 2,
    Ping = 3,
    Pong = 4,
    Error = 5,
    // M2
    RegisterRequest = 10,
    RegisterResponse = 11,
    LogoutRequest = 12,
    LogoutResponse = 13,
    TokenRenewRequest = 14,
    TokenRenewResponse = 15,
    ForceLogoutRequest = 16,
    ForceLogoutResponse = 17,
};

enum class ErrorCode : int
{
    Ok = 0,
    // 1xxx: 请求相关
    InvalidRequest = 1000,
    UnsupportedVersion = 1001,
    // 2xxx: 认证相关
    AuthenticationFailed = 2001,
    AccountAlreadyExists = 2002,
    AccountNotFound = 2003,
    SessionExpired = 2004,
    SessionInvalid = 2005,
    LoginRateLimited = 2006,
    TooManyDevices = 2007,
    // 9xxx: 系统相关
    Timeout = 9001,
    InternalError = 9002,
};

struct Packet
{
    quint16 version = CurrentVersion;
    MessageType messageType = MessageType::Error;
    quint64 requestId = 0;
    QByteArray payload;
};
}
