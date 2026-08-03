# XYChat 协议文档

## M5 当前协议状态

M5 在 M3 基础上，新增了传输层加密（TLS 1.2+）和重放保护机制。服务端与客户端均已实现 TLS 与 timestamp/nonce 字段，但 **2026-08-03 代码审查确认这些能力当前可降级/可绕过**：TLS 初始化失败时服务端仍以明文 TCP 启动，客户端找不到 CA 时会退回明文连接，重放保护字段在服务端并非强制校验。在 M5.5 安全加固完成前，本协议不应被视为生产级安全协议。

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
  "password": "<plaintext-password>",
  "clientVersion": "0.2.0",
  "platform": "windows",
  "deviceId": "<machine-id-hex>",
  "timestamp": 1753977600,
  "nonce": "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
}
```

- 当前客户端（`NetworkManager::sendLoginRequest`）直接传输 QML 输入的**原始密码**，依赖 TLS 保护传输过程；`encryptPassword()`（SHA-256 摘要）虽存在但登录流程未调用
- 服务端使用 PBKDF2-HMAC-SHA256（100,000 次迭代 + 16 字节随机盐）存储密码验证数据
- 登录成功后返回 session token

> 2026-08-03 校正：旧版本文档描述登录传输 SHA-256 摘要，与代码实现不符。SHA-256 预散列对传输安全没有实质增益（摘要本身即成为传输凭据），保持明文 + TLS 的做法与主流 IM 一致；在 TLS 强制开启（fail-closed）之前，登录凭据存在明文传输风险。

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

> ⚠️ 已知安全缺陷：服务端接受请求中任意 `userId` 并注销该账号的全部会话，构成越权注销。计划修复方向：删除该公开接口，或改为“仅注销本人其他设备”（以 session/设备为目标的 `terminate_session`），管理员踢人能力须独立鉴权通道。

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

1. 客户端连接服务端（当前 TLS 可选：CA 缺失或 TLS 初始化失败时双方都会退回明文）
2. 发送 `LoginRequest`，服务端验证密码后返回 session token
3. 后续请求依赖连接级认证状态（`RequestHandler` 内存中的 `m_authenticatedUserId`/`m_currentSessionId`），不在每个请求中验证 token
4. 客户端可发送 `TokenRenewRequest` 续期 token（请求携带的 `token` 字段当前未被服务端校验）
5. 客户端发送 `LogoutRequest` 主动登出
6. 服务端在连接断开时自动清理 session

> 已知限制（P1）：断线重连后必须重新登录；多端登录、token 撤销与续期语义不一致。目标方案：每个认证命令显式携带 access token 或将其安全绑定到已认证的 TLS channel，撤销后即时失效。

### 限流策略

- 同一 IP 在 5 分钟内最多 10 次登录失败
- 同一用户在 5 分钟内最多 5 次登录失败
- 触发限流后返回 `LoginRateLimited` 错误

## 已知限制

- 当前协议兼容策略只支持版本 `1`，后续版本升级需要扩展协商或降级策略。
- Session token 当前通过 handler 内存状态验证，尚未在每次请求中传递 token；`TokenRenewRequest` 携带的 token 未被服务端使用。
- 重放保护的 nonce 缓存为每连接级别，服务端重启后清空，且缓存超限后直接全量清空（见下文）。
- `sync_messages` / `ack_message` 缺少会话成员/消息归属授权，存在越权读取与越权改状态风险（P0，待修复）。
- 消息发送无客户端幂等键（`client_message_id`），重试会产生重复消息。
- 消息状态为全局单值（`messages.status`），无法在多设备下正确聚合送达/已读。

## M5 新增：传输层加密

- 服务端使用 `QSslSocket` + TLS 1.2+ 监听。
- 客户端使用 `connectToHostEncrypted()` 建立加密连接。
- 开发环境自动生成自签名 CA + 服务端证书（SAN: localhost, 127.0.0.1）。
- 证书错误时客户端拒绝连接并提示用户。

> ⚠️ 当前实现可静默降级：`Server` 在 `initTls()` 失败后仍以明文 TCP 启动；客户端 `NetworkManager::initTls()` 找不到 CA 时保持 `m_tlsEnabled = false` 并以明文连接。目标修复：生产模式 fail-closed（TLS 不可用则拒绝启动/连接），开发明文模式改为显式、默认关闭的配置项。

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

服务端重放保护当前实现：
- 时间戳容差：±300 秒（5 分钟）
- nonce 缓存：每连接维护，上限 10000 条（超限后全量清空）
- Ping/Pong 心跳不要求重放保护字段

> ⚠️ 当前实现可绕过：`checkReplayProtection()` 仅在 timestamp/nonce 非空时校验，攻击者省略字段即可完全绕过。目标修复：timestamp/nonce 改为必填，拒绝缺失、格式错误、超时和重复的业务请求；nonce 按 session/设备维度持久化到 TTL 缓存，而非每连接内存集合。

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

> 计划变更（P1）：请求新增必填字段 `clientMessageId`（客户端生成的 UUID 幂等键），服务端以 `(sender_id, device_id, client_message_id)` 唯一约束去重，重试请求返回已存储的同一条消息；响应同步回传 `clientMessageId`。此为主流 IM（Telegram/WhatsApp/Signal）处理发送重试去重的标准做法。

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

> ⚠️ 已知安全缺陷（P0）：服务端仅校验消息存在，不校验请求者是否为消息所属会话的成员，任意已认证用户可修改任意消息状态。目标修复：先授权再更新，并改为按接收者维度的回执模型（`message_receipts`）。

#### 同步消息

```json
// 请求
{ "type": "sync_messages", "conversationId": 1, "afterId": 0, "limit": 100 }
// 响应 data
{ "conversationId": 1, "messages": [...], "hasMore": false }
```

> ⚠️ 已知安全缺陷（P0）：服务端仅校验会话存在（代码注释声称校验成员但实际未校验），任意已认证用户可拉取任意会话历史。目标修复：新增 `isConversationMember()` 授权；同时该接口为单会话拉取，启动时需逐会话扫描，计划在同步基础阶段引入按账号/设备游标的 `sync_events` 模型（参考 Telegram/WhatsApp 的增量同步方案）。

### 消息状态

| 状态 | 含义 |
| --- | --- |
| `sending` | 客户端正在发送 |
| `sent` | 服务端已接收并存储 |
| `delivered` | 接收方已收到 |
| `read` | 接收方已读 |
| `failed` | 发送失败 |

> 已知限制（P1）：状态存于 `messages.status` 全局单值，多设备收件时送达/已读无法正确聚合（一台设备已读即改变全局状态）。目标方案：新增 `message_receipts(message_id, user_id, device_id, delivered_at, read_at)` 表与会话成员读游标（`conversation_members.last_read_message_id` 已存在但语义待完善），`messages.status` 仅保留发送链路状态。

## 协议演进计划（M5.5 及之后）

基于 2026-08-03 代码审查与主流 IM（Telegram MTProto、WhatsApp/Signal 的同步与回执模型）参考，协议层计划变更：

| 变更 | 目标 | 参考 |
| --- | --- | --- |
| `timestamp`/`nonce` 强制必填并持久化去重 | 消除重放保护绕过 | Signal/Telegram 的 msg_id + salt 防重放 |
| 新增 `clientMessageId` 幂等键 | 重试不重复写消息 | Telegram `random_id`、WhatsApp 客户端消息 ID |
| 新增 `sync_events` + 账号/设备游标接口 | 启动增量同步，覆盖消息/联系人/回执/删除 | Telegram updates.getChannelDifference 式差分同步 |
| `ack_message` 改为接收者回执模型 | 多设备送达/已读聚合 | WhatsApp 蓝勾模型（per-recipient receipt） |
| `force_logout` 改为 `terminate_session` | 仅允许注销本人其他设备 | Telegram sessions.killSession |
| 认证命令显式携带 token 或绑定 TLS channel | 断线重连、撤销、多端语义一致 | OAuth2 access token / MTProto auth_key 绑定 |

上述变更将在对应修复落地并补充自动化测试后，在本文件中更新字段定义并标注版本。
