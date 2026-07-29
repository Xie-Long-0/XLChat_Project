# XYChat 安全文档

## M3 当前安全状态

### 密码存储

- 客户端对密码执行 SHA-256 摘要后发送给服务端。
- 服务端使用 **PBKDF2-HMAC-SHA256** 对密码进行慢哈希存储。
- 存储格式：`v1:<iterations>:<salt-hex>:<hash-hex>`
  - `v1`：参数版本号，便于后续升级迭代
  - `iterations`：当前默认 100,000 次迭代
  - `salt`：16 字节随机盐，每次注册/修改密码独立生成
  - `hash`：32 字节 PBKDF2 派生结果
- 密码验证使用常数时间比较，防止时序攻击。

### Session Token

- 登录成功后服务端生成 32 字节随机 token（OpenSSL RAND_bytes）。
- 服务端存储 token 的 SHA-256 摘要，而非明文 token。
- Session 默认有效期 7 天。
- 支持 token 续期（重新生成 token 并替换旧 session）。
- 支持主动登出和强制下线。

### 登录限流

- 同一 IP 在 5 分钟内最多允许 10 次登录失败。
- 同一用户在 5 分钟内最多允许 5 次登录失败。
- 触发限流后返回 `LoginRateLimited` 错误码。
- 所有登录尝试（成功/失败）均记录到 `login_audit` 表。

### 数据库安全

- 数据库使用版本化迁移机制（`schema_version` 表），禁止隐式 schema 变更。
- 每个线程使用独立数据库连接名，避免多线程竞争。
- 表结构：`users`、`devices`、`sessions`、`login_audit`、`contacts`、`conversations`、`conversation_members`、`messages`。
- Session 表存储 token 哈希而非明文。
- 消息内容存储在 `messages` 表中，当前为明文存储（M5 将引入端到端加密）。

### 传输层

- 当前仍为普通 TCP，尚未启用 TLS（计划 M4）。
- 密码在传输层为 SHA-256 摘要，仍可能被重放。

## 风险

- SHA-256 传输摘要仍存在重放风险，M4 将引入 TLS 和 nonce。
- 普通 TCP 无法防止链路监听或中间人攻击。
- Session token 当前通过 handler 内存状态验证，未在每次请求中传递。
- 消息内容在服务端数据库为明文存储，M5 将引入端到端加密。

## 后续要求

- M4 将引入 TLS、证书校验、nonce 和重放保护。
- 日志不得输出明文密码、token、私钥或完整密钥材料。
- 后续可考虑将 PBKDF2 升级为 Argon2id。

