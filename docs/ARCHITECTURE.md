# XYChat 架构概览

> 2026-08-03 依据代码审查结果重写。旧文档存在三个历史版本拼接、M4 完成后仍描述 "Qt Widgets 客户端"、宣称客户端有本地缓存等与代码不符的内容，本次一并修正，修正原因在各节末标注。

## 当前组件（M5 完成后）

```text
Chat-Client ── QSslSocket/PacketCodec/JSON ── Chat-Server ── SQLite
     │             (TLS 1.2+，可降级*)            │
     └──── CommonModule（protocol/encryption/security）────┘
```

\* TLS 当前可静默降级为明文 TCP，属 P0 缺陷，见下文"已知架构问题"。

- `Chat-Client`：Qt 桌面客户端，**UI 已全面采用 QML/Qt Quick**（M4 完成），通过 `QWindowKit::Quick` 实现无边框窗口；C++ 后端层为 `core/NetworkManager`（网络状态机、协议编解码、TLS）与 `models/User`。
- `Chat-Server`：Qt TCP 服务端，`ConnectionServer`（QTcpServer）接受连接，每连接一个 `RequestHandler`（QThread）处理注册/登录/登出/续期/联系人/消息请求，管理 session 路由与在线状态，访问 SQLite。
- `CommonModule`：客户端和服务端共享代码：
  - `protocol/`：`Packet` / `PacketCodec` 长度前缀帧协议；
  - `encryption/`：`EncryptionManager`（PBKDF2 慢哈希 + Token 生成）；
  - `security/`：`TlsHelper`（证书生成/加载）、`LogSanitizer`（日志脱敏）、`SecureMemory`（敏感内存清零）。
- `docs`：路线图、协议、安全和架构说明。
- `tests`：Qt Test 单元测试（PacketCodec、EncryptionManager、DatabaseManager、Security）。

## 服务端运行模型

```text
ConnectionServer(主线程) ── socketAccepted ──> RequestHandler(QThread, 每连接一个)
     │                                              ├── QSslSocket（handler 线程内创建）
     │                                              ├── DatabaseManager（每线程独立连接名）
     │                                              └── PacketCodec + 认证状态(内存)
     └── Server(主线程)：在线路由表 userId -> {sessionId -> handler}
            └── onMessageForUser: 向目标用户所有在线 handler 转发数据包
```

- 每连接一线程模型在低连接数下可行；连接数上升后成本高（ROADMAP 风险清单已记录）。
- 跨线程推送使用 `QMetaObject::invokeMethod` 队列调用。

## 当前能力边界

### 协议层（M1）

- 长度前缀帧协议（magic + version + messageType + requestId + payloadLength + payload）。
- 统一响应结构（code / message / data），所有请求通过 requestId 匹配。
- ping/pong 心跳与空闲超时（90 秒）。

### 账户体系（M2）

- 注册、登录、登出、token 续期、强制下线。
- 密码存储使用 PBKDF2-HMAC-SHA256（100,000 次迭代 + 16 字节随机盐）。
- 登录后签发 session token，服务端维护 `sessions` 表。
- 登录失败限流：同一 IP 5 分钟 10 次、同一用户 5 分钟 5 次。
- **限制**：认证状态保存在 `RequestHandler` 内存中，断线重连必须重新登录；token 续期请求中的 token 未被服务端校验。

### 即时通信（M3）

- 用户搜索、联系人关系（双向）。
- 会话模型（conversations / conversation_members）。
- 消息表（messages），服务端递增 ID，支持消息状态（sending/sent/delivered/read/failed）。
- 服务端实现 send_message / ack_message / sync_messages，在线用户通过 `messageForUser` 信号实时路由。
- 客户端实现会话列表、聊天窗口、消息气泡。
- 离线消息通过 sync_messages（afterId 游标）按会话增量同步。
- **限制**：客户端无本地缓存、无离线 outbox、无历史分页触发；"重启后仍显示历史消息"依赖重新拉取，不是本地持久化。sync_messages 无会话成员授权（P0）。

### 传输层安全（M5）

- 服务端 `QSslSocket` + TLS 1.2+，开发环境自签 CA（`certs/` 脚本生成）。
- 客户端校验服务端证书，证书错误时断开。
- 业务请求携带 timestamp/nonce 重放保护字段；日志脱敏；敏感内存清零。
- **限制**：TLS 与重放保护当前可降级/可绕过，不是可作生产安全承诺的完整控制。

## 数据库 Schema（V3）

- `schema_version`：数据库迁移版本控制
- `users`：用户基础信息（username, email, phone, password_hash）
- `devices`：设备信息（device_id, device_name, platform, public_key）——`public_key` 仅为预留列，当前无已落地的 E2EE 公钥流程
- `sessions`：登录会话（token_hash, login_ip, expires_at, last_active_at）
- `login_audit`：登录审计日志（ip_address, success, failure_reason）
- `contacts`：联系人关系（双向记录）
- `conversations`：会话信息（type, last_message_id, updated_at）
- `conversation_members`：会话成员（conversation_id, user_id, last_read_message_id, role）
- `messages`：消息主体（conversation_id, sender_id, content, status, created_at）——消息正文为服务端可读明文，E2EE 属 M6 目标

### Schema V4 计划（随 M5.5/M9 落地）

| 变更 | 目的 |
| --- | --- |
| `messages` 增加 `client_message_id` + `UNIQUE(sender_id, device_id, client_message_id)` | 发送幂等，重试不重复写（Telegram `random_id` 模式） |
| 新增 `message_receipts(message_id, user_id, device_id, delivered_at, read_at)` | 多设备送达/已读聚合，替代全局 `messages.status`（WhatsApp 回执模型） |
| 新增 `sync_events(seq, user_id, type, payload, created_at)` + 设备游标 | 账号级增量同步，覆盖消息/联系人/回执/删除 |

## 客户端架构（M4 已落地）

```text
Chat-Client
  ├── QML UI 层（resources/）
  │     ├── main.qml（入口）
  │     ├── pages/（LoginPage.qml, MainPage.qml, MainWindow.qml）
  │     ├── components/（TitleBar, ConversationList, ChatView, MessageInput, MessageBubble, QWKButton）
  │     └── theme/（Theme.qml 单例，qmldir 注册）
  ├── C++ 后端层
  │     ├── core/NetworkManager（连接状态机 + TLS + 协议，注册为 QML 上下文对象）
  │     └── models/User
  └── QWindowKit（QWK::Quick WindowAgent：无边框、拖拽、Snap Layout）
```

与旧文档的差异说明：

- 主题目前**只有 `Theme.qml` 单一主题**，`DarkTheme.qml`/`LightTheme.qml` 与亮暗切换尚未实现（ROADMAP M4 验收项"支持亮色/暗色主题切换"实际未达成，已在路线图更正）。

- 客户端尚无独立模型层/本地数据库；`models/User` 仅是登录态数据对象。

## 已知架构问题（源自 2026-08-03 审查）

| 级别 | 问题 | 修复方向 |
| --- | --- | --- |
| P0 | 会话/消息接口缺成员授权（sync_messages 仅查会话存在，ack_message 仅查消息存在） | 新增 `isConversationMember()` / `canAccessMessage()`，所有会话、回执、历史接口先授权再查询 |
| P0 | `force_logout` 接受任意 `userId`，构成越权注销 | 改为 `terminate_session`（仅本人其他设备），管理员能力独立鉴权 |
| P0 | TLS 可静默降级（服务端 initTls 失败仍启动；客户端缺 CA 走明文） | 生产 fail-closed；开发明文模式显式、默认关闭 |
| P0 | timestamp/nonce 非必填，可整体绕过多放保护 | 强制必填 + 格式校验 + TTL 持久化去重（按 session/设备） |
| P1 | 认证依赖 handler 内存状态，token 语义不完整 | access token / TLS channel 绑定，撤销即时失效 |
| P1 | `RequestHandler`(QThread) 与 socket 线程归属、`sendRawData` 排队写存在线程风险 | worker QObject + `moveToThread()` 或 handler 线程专用发送槽，补集成测试 |
| P1 | 消息无客户端幂等键，无 outbox | `client_message_id` + 唯一约束；客户端本地 outbox 重试 |
| P1 | 单值 `messages.status` 无法多设备聚合 | `message_receipts` + 成员读游标 |
| P1 | `sync_messages` 单会话拉取 | `sync_events` 账号/设备游标模型，实时通知仅触发增量拉取 |

## 架构调整依据

修复方向参考主流 IM 的公开技术方案：

- **Telegram**：MTProto 以 auth_key 绑定加密通道、`random_id` 幂等去重、`getDifference/getChannelDifference` 差分同步、`sessions.killSession` 设备管理。
- **WhatsApp**：per-recipient 送达/已读回执（蓝勾模型）、客户端消息 ID 去重。
- **Signal**：预密钥（pre-key）离线密钥协商、消息级 MAC 防篡改（M6 E2EE 参考）。

共同原则：先授权再查询（authorization-before-query）、fail-closed 的传输安全、幂等写 + 游标拉（idempotent write, cursor-based pull）、推送只做通知、数据靠增量同步兜底。本项目 P0/P1 修复与 M9 同步模型均按这些原则设计。

## 下一步演进

1. **M5.5（新增）：安全加固**——完成上表全部 P0/P1 修复并补齐自动化测试（越权拒绝、TLS 禁用时启动失败、nonce 缺失/重复拒绝、重试去重、多设备回执）。在此之前不新增功能里程碑。
2. **M6**：端到端加密一对一聊天（设备身份密钥、预密钥、消息 MAC）。
3. **M7+**：群聊、媒体、多端同步（`sync_events`）、搜索与通知。
