# XYChat 协议文档

## 当前协议状态（M5.5 完成后）

M5 在 M3 基础上新增了传输层加密（TLS 1.2+）与重放保护；**M5.5（2026-08-03 实施）完成了安全加固**：TLS 改为 fail-closed（初始化失败拒绝启动/连接，开发明文模式需显式开关）、timestamp/nonce 改为强制必填并全局 TTL 去重、会话/消息接口全部先授权再查询、越权注销接口改为仅能终止本人其他会话、发送消息新增 `clientMessageId` 幂等键、回执改为按接收者/设备维度记录、新增账号级 `sync_events` 游标同步。上述变更均有自动化测试覆盖。

仍属非生产级的部分：nonce 去重为单服务器内存缓存（重启清空）、认证状态仍为连接级（续期已校验 token，但其他请求未逐包验 token）、消息正文对服务端可读（E2EE 属 M6 目标）。

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
| `16` | `ForceLogoutRequest` | 会话终止请求（M5.5 起仅允许终止本人其他会话，兼容 `terminate_session` 类型名） |
| `17` | `ForceLogoutResponse` | 会话终止响应 |
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
| `39` | `MessageStatusUpdate` | 消息状态更新（服务端推送，M5.5 起由回执聚合触发） |
| `40` | `SyncEventsRequest` | 账号级增量同步请求（M5.5） |
| `41` | `SyncEventsResponse` | 账号级增量同步响应（M5.5） |

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

需要已认证 session。M5.5 起服务端会校验请求携带的 `token` 与当前 session 的 token 哈希是否一致，不一致返回 `SessionInvalid`。

```json
{
  "type": "token_renew",
  "token": "<current-session-token>"
}
```

### 会话终止请求（原强制下线）

需要已认证 session。M5.5 起 `force_logout` 的越权语义已移除：请求只能以 `sessionId` 或 `deviceId` 为目标，且目标必须属于**本人**的其他会话（不能终止当前会话，当前会话请用 `logout`）。新类型名 `terminate_session` 与旧名 `force_logout`、`messageType=16` 均兼容。

```json
{
  "type": "terminate_session",
  "sessionId": 5
}
```

```json
// 响应 data
{ "terminatedSessionId": 5 }
```

- 携带非本人的 `userId` 将被拒绝（`PermissionDenied`）。
- 被终止会话对应的连接会被服务端主动断开。
- 管理员踢人能力不在此接口范围内，需独立鉴权通道（未实现）。

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
| `1002` | `ReplayRejected` | 重放保护拒绝：timestamp/nonce 缺失、格式错误、超时或重复（M5.5） |
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
| `3006` | `PermissionDenied` | 越权访问被拒绝：非会话成员、非本人会话等（M5.5） |
| `9001` | `Timeout` | 连接空闲超时 |
| `9002` | `InternalError` | 服务端内部错误 |

### 心跳

客户端连接后定时发送 `Ping`，服务端返回相同 `requestId` 的 `Pong`。服务端连接空闲 90 秒会发送 `Timeout` 错误并断开连接。

### 认证流程

1. 客户端连接服务端（TLS fail-closed：服务端无证书拒绝启动，客户端无 CA 拒绝连接；开发明文需显式开关）
2. 发送 `LoginRequest`，服务端验证密码后返回 session token
3. 后续请求依赖连接级认证状态（`RequestHandler` 内存中的 `m_authenticatedUserId`/`m_currentSessionId`）；M5.5 起 `TokenRenewRequest` 会真正校验携带的 token
4. 客户端可发送 `TokenRenewRequest` 续期 token（旧 session 删除，新 session 生效）
5. 客户端发送 `LogoutRequest` 主动登出；或用 `terminate_session` 终止本人其他设备的会话
6. 服务端在连接断开时自动清理 session

> 已知限制：断线重连后必须重新登录；除续期外的命令尚未逐包验证 token。后续方向：每个认证命令显式携带 access token 或绑定已认证 TLS channel，撤销后即时失效。

### 限流策略

- 同一 IP 在 5 分钟内最多 10 次登录失败
- 同一用户在 5 分钟内最多 5 次登录失败
- 触发限流后返回 `LoginRateLimited` 错误

## 已知限制

- 当前协议兼容策略只支持版本 `1`，后续版本升级需要扩展协商或降级策略。
- Session token 通过连接级认证状态维护；仅 `TokenRenewRequest` 逐包校验 token（M5.5），其他命令尚未逐包验证。
- nonce 去重缓存为单服务器内存 TTL 缓存（跨连接共享），服务端重启后清空；多服务器部署时需改为持久化存储。
- 消息正文对服务端可读，E2EE 属 M6 目标。
- 会话删除/消息撤回尚未实现，`sync_events` 暂无对应事件类型。

## M5 新增：传输层加密

- 服务端使用 `QSslSocket` + TLS 1.2+ 监听。
- 客户端使用 `connectToHostEncrypted()` 建立加密连接。
- 开发环境自动生成自签名 CA + 服务端证书（SAN: localhost, 127.0.0.1）。
- 证书错误时客户端拒绝连接并提示用户。

**M5.5 fail-closed 策略**：

- 服务端：`initTls()` 失败时 `start()` 拒绝启动；仅当显式传入 `--allow-plaintext` 时才允许明文 TCP（仅开发用途，启动日志明确告警）。
- 客户端：找不到或无法加载 CA 时拒绝连接并通过登录/注册失败信号提示用户；仅当显式设置环境变量 `XYCHAT_ALLOW_PLAINTEXT=1` 时才允许明文连接。
- 因此不存在静默降级路径：要么 TLS，要么显式声明的开发明文。

## M5 新增：重放保护

所有业务请求（登录、注册、登出、消息等）的 JSON payload 中携带以下字段（**M5.5 起均为必填**）：

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
| `timestamp` | int64 | Unix 秒级时间戳，必填；服务端拒绝缺失、非数值或偏差超过 300 秒的请求 |
| `nonce` | string | UUID v4 随机字符串，必填（≤128 字符）；服务端拒绝重复 nonce |

服务端重放保护实现（M5.5）：
- 缺失、格式错误、超时、重复的业务请求一律拒绝，返回 `ReplayRejected (1002)`
- 时间戳容差：±300 秒（5 分钟）
- nonce 去重：服务端全局共享的 `NonceCache`（跨连接生效），TTL 600 秒惰性清理，上限 100000 条
- Ping/Pong 心跳不要求重放保护字段

## 测试覆盖

- `TestPacketCodec::parsesManyConsecutiveSmallPackets` 覆盖连续 1000 个小包解析。
- `TestPacketCodec::waitsForSplitLargePacket` 覆盖单个大包拆成多次到达后的解析。
- `TestEncryptionManager` 覆盖 PBKDF2 哈希、验证、token 生成。
- `TestDatabaseManager` 覆盖迁移（V1-V4）、用户注册、session 管理（含按 ID 查询 token 哈希）、登录审计、设备管理、联系人、会话、消息；M5.5 新增：会话成员/消息访问授权、`clientMessageId` 幂等去重、回执聚合、读游标单调前进、`sync_events` 游标。
- `TestSecurity` 覆盖日志脱敏、安全内存清零、TLS 证书生成与加载；M5.5 新增：nonce 首次接受/重复拒绝/空值拒绝/TTL 过期。
- 客户端登录响应按 `requestId` 匹配，不处理不属于当前登录请求的响应。
- 尚缺：真实 TLS 客户端-服务端集成测试、并发路由与端到端消息测试（服务端授权逻辑已有数据库层测试覆盖）。

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
// 请求（M5.5 起 clientMessageId 必填）
{ "type": "send_message", "toUserId": 2, "content": "Hello!", "contentType": "text", "clientMessageId": "<uuid>" }
// 响应 data
{ "messageId": 1, "conversationId": 1, "clientMessageId": "<uuid>", "status": "sent" }
```

`clientMessageId` 为客户端生成的 UUID 幂等键（参考 Telegram `random_id`/WhatsApp 客户端消息 ID）：服务端以 `(sender_id, sender_device_id, client_message_id)` 唯一约束去重，重试/重连重发返回已存储的同一条消息；客户端维护 outbox，登录成功后自动重发未确认消息。

#### 新消息通知（服务端推送）

```json
{ "messageId": 1, "conversationId": 1, "senderId": 2, "content": "Hi!", "contentType": "text", "createdAt": "..." }
```

#### 消息确认（回执）

```json
// 请求
{ "type": "ack_message", "messageId": 1, "status": "delivered" }
// 响应 data
{ "messageId": 1, "status": "delivered" }
```

M5.5 行为：
- 先授权再更新：请求者必须是消息所属会话的成员，否则返回 `PermissionDenied (3006)`。
- `status` 仅接受 `delivered` / `read`。
- 回执写入 `message_receipts(message_id, user_id, device_id, delivered_at, read_at)`，按接收者/设备维度记录（参考 WhatsApp per-recipient 回执模型）；多设备各自回执互不覆盖。
- 服务端根据回执聚合更新 `messages.status` 展示值，并向发送方推送 `MessageStatusUpdate`，同时写入发送方 `sync_events`。

#### 同步消息

```json
// 请求
{ "type": "sync_messages", "conversationId": 1, "afterId": 0, "limit": 100 }
// 响应 data
{ "conversationId": 1, "messages": [...], "hasMore": false }
```

M5.5 行为：先授权再查询 —— 非会话成员返回 `PermissionDenied (3006)`；拉取仅前进成员读游标，不再隐式修改全局消息状态（已读回执由显式 `ack_message` 产生）。

#### 账号级增量同步（M5.5 新增）

```json
// 请求
{ "type": "sync_events", "afterSeq": 0, "limit": 200 }
// 响应 data
{ "events": [{ "seq": 1, "type": "message", "payload": { ... }, "createdAt": "..." }], "lastSeq": 1, "hasMore": false }
```

- 事件流按账号维度严格递增（`seq`），客户端保存 `lastSeq` 游标做增量拉取（参考 Telegram 差分同步模型）。
- 当前事件类型：`message`（新消息）、`contact_added`（联系人变更）、`receipt`（送达/已读回执）。
- 实时推送（`NewMessageNotification`/`MessageStatusUpdate`）仅作为通知，离线或丢推送时由 `sync_events` 兜底补齐。

### 消息状态

| 状态 | 含义 |
| --- | --- |
| `sending` | 客户端正在发送 |
| `sent` | 服务端已接收并存储 |
| `delivered` | 接收方已收到 |
| `read` | 接收方已读 |
| `failed` | 发送失败 |

> M5.5 说明：送达/已读的权威记录在 `message_receipts`（按接收者/设备维度，支持多设备聚合）；`messages.status` 仅作为由回执聚合得出的展示值，发送链路状态（sending/sent/failed）仍由客户端维护。

## 协议演进记录（M5.5 已实施）

以下变更基于 2026-08-03 代码审查与主流 IM（Telegram MTProto、WhatsApp/Signal 的同步与回执模型）参考，已在 M5.5 落地并附自动化测试：

| 变更 | 状态 | 参考 |
| --- | --- | --- |
| `timestamp`/`nonce` 强制必填 + 全局 TTL 去重 | ✅ 已实施 | Signal/Telegram 的 msg_id + salt 防重放 |
| `clientMessageId` 幂等键 + 唯一约束 + 客户端 outbox | ✅ 已实施 | Telegram `random_id`、WhatsApp 客户端消息 ID |
| `sync_events` + 账号游标接口 | ✅ 已实施（消息/联系人/回执） | Telegram updates 差分同步 |
| `ack_message` 接收者回执模型（`message_receipts`） | ✅ 已实施 | WhatsApp 蓝勾模型（per-recipient receipt） |
| `force_logout` → `terminate_session`（仅本人会话） | ✅ 已实施 | Telegram sessions.killSession |
| 续期接口真正校验 token | ✅ 已实施 | OAuth2 access token 验证 |
| 全部命令逐包携带并验证 access token / TLS channel 绑定 | ⬜ 未实施（后续） | MTProto auth_key 绑定 |

后续协议方向：消息撤回/编辑/删除事件纳入 `sync_events`；群聊与 fan-out 策略（M7）；媒体分片上传走独立通道（M8）。
