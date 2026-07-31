# XYChat 安全文档

## M5 当前安全状态

### 传输层安全 (TLS)

- 服务端使用 `QSslSocket` + TLS 1.2+ 加密所有客户端连接。
- 客户端使用 `QSslSocket` + `connectToHostEncrypted()`，强制验证服务端证书。
- 证书错误时客户端明确拒绝连接并向用户提示错误信息。
- 开发环境：服务端首次启动自动生成自签名 CA + 服务端证书（RSA 2048，SHA-256 签名，SAN: localhost/127.0.0.1）。
- 生产环境：替换 `certs/` 目录下的证书文件为正式 CA 签发的证书即可。
- 证书生成使用 OpenSSL X509 API，文件 I/O 通过内存 BIO + Qt QFile 避免 Windows applink 问题。

### 重放保护

- 所有业务请求携带 `timestamp`（Unix 秒级时间戳）和 `nonce`（UUID v4）。
- 服务端拒绝时间戳偏差超过 5 分钟的请求。
- 服务端维护每连接 nonce 缓存，拒绝重复 nonce。
- nonce 缓存上限 10000 条，超出后清空重建。

### 日志脱敏

- 提供 `LogSanitizer` 工具类，对密码、token、消息正文、IP 地址、邮箱进行掌码处理。
- 日志中不输出明文密码、完整 token、私钥或完整消息正文。
- IP 地址只保留前两段（如 `192.168.*.*`）。

### 安全内存

- 提供 `SecureMemory` 工具类，使用 `OPENSSL_cleanse()` 安全清零内存。
- 密码、token 等敏感数据在使用完毕后立即安全清除。
- 客户端登出时对 session token、待处理密码执行安全清零。

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

- 已启用 TLS 1.2+，所有客户端-服务端通信均加密。
- 服务端使用 `QSslSocket::startServerEncryption()`，客户端使用 `connectToHostEncrypted()`。
- 开发证书自动生成，有效期 10 年。
- 拓包无法直接看到登录凭据或消息正文。

## 风险

- 开发环境使用自签证书，生产环境必须替换为正式 CA 证书。
- Session token 当前通过 handler 内存状态验证，未在每次请求中传递。
- 消息内容在服务端数据库为明文存储，M6 将引入端到端加密。
- 重放保护的 nonce 缓存为每连接级别，服务端重启后清空。

## 后续要求

- M6 将引入端到端加密（E2EE），服务端将无法解密消息正文。
- 后续可考虑将 PBKDF2 升级为 Argon2id。
- 生产部署时应启用证书自动续期或 ACME 协议。
- 可考虑增加 HSTS 或证书固定 (Certificate Pinning) 策略。

