# XYChat 协议文档

## M2 当前协议状态

M2 在 M1 长度前缀帧协议基础上，新增了账户注册、session token 认证、登出、token 续期和强制下线功能。密码存储升级为 PBKDF2-HMAC-SHA256 慢哈希。

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

- 当前传输仍是明文 TCP，尚未启用 M4 的 TLS。
- 当前心跳只做连接保活和空闲断开，尚未包含重放保护、nonce 或会话绑定。
- 当前协议兼容策略只支持版本 `1`，后续版本升级需要扩展协商或降级策略。
- Session token 当前通过 handler 内存状态验证，尚未在每次请求中传递 token。

## 测试覆盖

- `TestPacketCodec::parsesManyConsecutiveSmallPackets` 覆盖连续 1000 个小包解析。
- `TestPacketCodec::waitsForSplitLargePacket` 覆盖单个大包拆成多次到达后的解析。
- `TestEncryptionManager` 覆盖 PBKDF2 哈希、验证、token 生成。
- `TestDatabaseManager` 覆盖迁移、用户注册、session 管理、登录审计、设备管理。
- 客户端登录响应按 `requestId` 匹配，不处理不属于当前登录请求的响应。
