# XYChat 架构概览

## 当前组件

```text
Chat-Client ── QTcpSocket/PacketCodec/JSON ── Chat-Server ── SQLite
     │                                          │
     └──── CommonModule/encryption/protocol ───┘
```

- `Chat-Client`：Qt Widgets 桌面客户端，负责登录/注册界面、主窗口与网络请求。
- `Chat-Server`：Qt TCP 服务端，负责监听连接、处理注册/登录/登出/续期请求、管理 session、访问 SQLite 数据库。
- `CommonModule`：客户端和服务端共享代码，提供 `EncryptionManager`（SHA-256 摘要 + PBKDF2 慢哈希 + Token 生成）和 `Packet`/`PacketCodec` 协议层。
- `docs`：路线图、协议、安全和架构说明。
- `tests`：自动化测试入口。

## 当前边界

- 客户端与服务端通过长度前缀帧协议传输 JSON payload，已经处理粘包/拆包、协议版本和请求 ID。
- 服务端使用 SQLite，每线程独立数据库连接名。
- 密码存储使用 PBKDF2-HMAC-SHA256（100,000 次迭代 + 16 字节随机盐）。
- 登录后签发 session token，服务端维护 `sessions` 表。
- 支持注册、登录、登出、token 续期、强制下线。
- 登录失败限流：同一 IP 5 分钟 10 次、同一用户 5 分钟 5 次。

## 数据库 Schema（V2）

- `schema_version`：数据库迁移版本控制
- `users`：用户基础信息（username, email, phone, password_hash）
- `devices`：设备信息（device_id, device_name, platform, public_key）
- `sessions`：登录会话（token_hash, login_ip, expires_at, last_active_at）
- `login_audit`：登录审计日志（ip_address, success, failure_reason）

## 下一步演进

M3 将实现一对一文本聊天 MVP：消息表、发送/接收消息、离线消息同步、聊天界面。
# XYChat 架构概览

## 当前组件

```text
Chat-Client ── QTcpSocket/PacketCodec/JSON ── Chat-Server ── SQLite
     │                                          │
     └──── CommonModule/encryption/protocol ───┘
```
