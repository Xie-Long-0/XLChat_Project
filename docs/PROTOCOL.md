# XYChat 协议文档

## M5 当前协议状态

M5 在 M3 基础上，新增了传输层加密（TLS 1.2+）和重放保护机制。所有客户端-服务端通信均通过 TLS 加密传输，所有业务请求携带时间戳和 nonce 防止重放攻击。

### 固定包头

所有多字节整数使用大端序。包头长度为 20 字节：

```text
magic:u32 | version:u16 | messageType:u16 | requestId:u64 | payloadLength:u32 | payload
```

字段说明：

| 字段 | 当前值/说明 |
| --- | --- |
| `magic` | `0x58594350`，ASCII 语义为 `XYCP` |
| `version` | 当前协议版本为 `1` |
| `messageType` | 见下表 |
| `requestId` | 客户端生成的请求 ID；响应沿用请求 ID |
| `payloadLength` | JSON payload 字节数，当前最大 4 MiB |

### 消息类型

| 值 | 名称 | 说明 |
| --- | --- | --- |
| `1` | `LoginRequest` | 登录请求 |
| `2` | `LoginResponse` | 登录响应 |
| `3` | `Ping` | 心跳请求 |
| `4` | `Pong` | 心跳响应 |
| `5` | `Error` | 错误 |
| `10` | `RegisterRequest` | 注册请求 |
| `11` | `RegisterResponse` | 注册响应 |
| `12` | `LogoutRequest` | 登出请求 |
| `13` | `LogoutResponse` | 登出响应 |
| `14` | `TokenRenewRequest` | Token 续期请求 |
| `15` | `TokenRenewResponse` | Token 续期响应 |
| `16` | `ForceLogoutRequest` | 强制下线请求 |
| `17` | `ForceLogoutResponse` | 强制下线响应 |
| `20` | `SearchUsersRequest` | 用户搜索请求 |
| `21` | `SearchUsersResponse` | 用户搜索响应 |
| `22` | `AddContactRequest` | 添加联系人请求 |
| `23` | `AddContactResponse` | 添加联系人响应 |
| `24` | `GetContactsRequest` | 获取联系人列表请求 |
| `25` | `GetContactsResponse` | 获取联系人列表响应 |
| `30` | `GetConversationsRequest` | 获取会话列表请求 |
| `31` | `GetConversationsResponse` | 获取会话列表响应 |
| `32` | `SendMessageRequest` | 发送消息请求 |
| `33` | `SendMessageResponse` | 发送消息响应 |
| `34` | `NewMessageNotification` | 新消息通知（服务端推送） |
| `35` | `AckMessageRequest` | 消息确认请求 |
| `36` | `AckMessageResponse` | 消息确认响应 |
| `37` | `SyncMessagesRequest` | 同步消息请求 |
| `38` | `SyncMessagesResponse` | 同步消息响应 |
| `39` | `MessageStatusUpdate` | 消息状态更新 |

### 注册请求

```json
{
  "type": "register",
  "username": "newuser",
  "password": "<plaintext-password>",
  "email": "user@example.com",
  "phone": "13800000000"
}
```

- `username`：必填，3-32 字符
- `password`：必填，至少 6 字符，服务端使用 PBKDF2-HMAC-SHA256 存储
- `email`：可选
- `phone`：可选

### 注册响应

```json
{
  "code": 0,
  "message": "Account created successfully",
  "data": {
    "userId": 1,
    "username": "newuser"
  }
}
```

### 登录请求

```json
{
  "type": "login",
  "username": "admin",
  "password": "<sha256-hex>",
  "clientVersion": "0.2.0",
  "platform": "windows",
  "deviceId": "<machine-id-hex>"
}
```

- 客户端对密码执行 SHA-256 摘要后发送，服务端使用 PBKDF2 与存储的哈希比较
- 登录成功后返回 session token

### 登录响应

```json
{
  "code": 0,
  "message": "OK",
  "data": {
    "username": "admin",
    "userId": 1,
    "token": "<session-token-hex>",
    "expiresAt": "2026-08-05T12:00:00"
  }
}
```

### 登出请求

需要已认证 session。

```json
{
  "type": "logout"
}
```

### Token 续期请求

需要已认证 session。

```json
{
  "type": "token_renew",
  "token": "<current-session-token>"
}
```

### 强制下线请求

需要已认证 session。

```json
{
  "type": "force_logout",
  "userId": 2
}
```

### 标准响应

所有响应 payload 统一为：

```json
{
  "code": 0,
  "message": "OK",
  "data": {}
}
```

### 错误码

| code | 名称 | 含义 |
| --- | --- | --- |
| `0` | `Ok` | 成功 |
| `1000` | `InvalidRequest` | 请求格式、类型或 payload 非法 |
| `1001` | `UnsupportedVersion` | 协议版本不支持 |
| `2001` | `AuthenticationFailed` | 用户名或密码错误 |
| `2002` | `AccountAlreadyExists` | 用户名已存在 |
| `2003` | `AccountNotFound` | 用户不存在 |
| `2004` | `SessionExpired` | Session 已过期 |
| `2005` | `SessionInvalid` | Session 无效或未认证 |
| `2006` | `LoginRateLimited` | 登录失败次数过多，触发限流 |
| `2007` | `TooManyDevices` | 设备数量超限 |
| `3001` | `ContactAlreadyExists` | 联系人已存在 |
| `3002` | `ContactNotFound` | 联系人不存在 |
| `3003` | `ConversationNotFound` | 会话不存在 |
| `3004` | `MessageNotFound` | 消息不存在 |
| `3005` | `CannotSendToSelf` | 不能给自己发送消息 |
| `9001` | `Timeout` | 连接空闲超时 |
| `9002` | `InternalError` | 服务端内部错误 |

### 心跳

客户端连接后定时发送 `Ping`，服务端返回相同 `requestId` 的 `Pong`。服务端连接空闲 90 秒会发送 `Timeout` 错误并断开连接。

### 认证流程

1. 客户端连接服务端
2. 发送 `LoginRequest`，服务端验证密码后返回 session token
3. 后续请求需要携带有效 session（当前通过 handler 状态维护）
4. 客户端可发送 `TokenRenewRequest` 续期 token
5. 客户端发送 `LogoutRequest` 主动登出
6. 服务端在连接断开时自动清理 session

### 限流策略

- 同一 IP 在 5 分钟内最多 10 次登录失败
- 同一用户在 5 分钟内最多 5 次登录失败
- 触发限流后返回 `LoginRateLimited` 错误

## 已知限制

- 当前协议兼容策略只支持版本 `1`，后续版本升级需要扩展协商或降级策略。
- Session token 当前通过 handler 内存状态验证，尚未在每次请求中传递 token。
- 重放保护的 nonce 缓存为每连接级别，服务端重启后清空。

## M5 新增：传输层加密

- 服务端使用 `QSslSocket` + TLS 1.2+ 监听。
- 客户端使用 `connectToHostEncrypted()` 建立加密连接。
- 开发环境自动生成自签名 CA + 服务端证书（SAN: localhost, 127.0.0.1）。
- 证书错误时客户端拒绝连接并提示用户。

## M5 新增：重放保护

所有业务请求（登录、注册、登出、消息等）的 JSON payload 中新增以下字段：

```json
{
  "type": "login",
  "timestamp": 1753977600,
  "nonce": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  ...
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `timestamp` | int64 | Unix 秒级时间戳，服务端拒绝偏差超过 300 秒的请求 |
| `nonce` | string | UUID v4 随机字符串，服务端拒绝重复 nonce |

服务端重放保护策略：
- 时间戳容差：±300 秒（5 分钟）
- nonce 缓存：每连接维护，上限 10000 条
- Ping/Pong 心跳不要求重放保护字段

## 测试覆盖

- `TestPacketCodec::parsesManyConsecutiveSmallPackets` 覆盖连续 1000 个小包解析。
- `TestPacketCodec::waitsForSplitLargePacket` 覆盖单个大包拆成多次到达后的解析。
- `TestEncryptionManager` 覆盖 PBKDF2 哈希、验证、token 生成。
- `TestDatabaseManager` 覆盖迁移、用户注册、session 管理、登录审计、设备管理、联系人、会话、消息。
- `TestSecurity` 覆盖日志脱敏、安全内存清零、TLS 证书生成与加载。
- 客户端登录响应按 `requestId` 匹配，不处理不属于当前登录请求的响应。

### M3 新增接口

#### 用户搜索

```json
// 请求
{ "type": "search_users", "query": "admin" }
// 响应 data
{ "users": [{ "userId": 1, "username": "admin" }] }
```

#### 添加联系人

```json
// 请求
{ "type": "add_contact", "userId": 2 }
// 响应 data
{ "contactUserId": 2 }
```

#### 获取联系人列表

```json
// 请求
{ "type": "get_contacts" }
// 响应 data
{ "contacts": [{ "userId": 2, "username": "bob", "addedAt": "..." }] }
```

#### 获取会话列表

```json
// 请求
{ "type": "get_conversations" }
// 响应 data
{ "conversations": [{ "conversationId": 1, "type": "private", "peerUserId": 2, "peerUsername": "bob", "lastMessage": "...", "unreadCount": 0 }] }
```

#### 发送消息

```json
// 请求
{ "type": "send_message", "toUserId": 2, "content": "Hello!", "contentType": "text" }
// 响应 data
{ "messageId": 1, "conversationId": 1, "status": "sent" }
```

#### 新消息通知（服务端推送）

```json
{ "messageId": 1, "conversationId": 1, "senderId": 2, "content": "Hi!", "contentType": "text", "createdAt": "..." }
```

#### 消息确认

```json
// 请求
{ "type": "ack_message", "messageId": 1, "status": "delivered" }
// 响应 data
{ "messageId": 1, "status": "delivered" }
```

#### 同步消息

```json
// 请求
{ "type": "sync_messages", "conversationId": 1, "afterId": 0, "limit": 100 }
// 响应 data
{ "conversationId": 1, "messages": [...], "hasMore": false }
```

### 消息状态

| 状态 | 含义 |
| --- | --- |
| `sending` | 客户端正在发送 |
| `sent` | 服务端已接收并存储 |
| `delivered` | 接收方已收到 |
| `read` | 接收方已读 |
| `failed` | 发送失败 |
