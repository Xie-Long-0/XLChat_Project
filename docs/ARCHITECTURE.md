# XYChat 架构概览

> 2026-08-03 依据代码审查结果重写，并于同日完成 M5.5 安全加固后再次更新；2026-08-04 完成 M4.5（M4 遗留清理与一对一聊天完善）后再次更新。

## 当前组件（M5.5 完成后）

```text
Chat-Client ── QSslSocket/PacketCodec/JSON ── Chat-Server ── SQLite
     │        (TLS 1.2+，fail-closed)          │
     └──── CommonModule（protocol/encryption/security）────┘
```

TLS 采用 fail-closed 策略：不存在静默降级路径（服务端无证书拒启，客户端无 CA 拒连；开发明文需显式开关）。

- `Chat-Client`：Qt 桌面客户端，**UI 已全面采用 QML/Qt Quick**（M4 完成，M4.5 完善），通过 `QWindowKit::Quick` 实现无边框窗口；登录窗口与主窗口为**两个独立根窗口**（均由 `main.cpp` 经 `engine.load()` 加载，主窗口在任务栏独立显示）；C++ 后端层为 `core/NetworkManager`（网络状态机、协议编解码、TLS）、`core/ThemeSettings`（主题偏好持久化）与 `models/User`。
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
     │                                              ├── 发送代理 QObject（handler 线程亲和，M5.5）
     │                                              ├── DatabaseManager（每线程独立连接名）
     │                                              └── PacketCodec + 认证状态(内存)
     └── Server(主线程)：在线路由表 userId -> {sessionId -> handler} + 全局 NonceCache
            ├── onMessageForUser: 向目标用户所有在线 handler 转发数据包
            └── onSessionTerminated: 终止本人其他会话时断开对应连接
```

- 每连接一线程模型在低连接数下可行；连接数上升后成本高（ROADMAP 风险清单已记录）。
- 跨线程推送统一投递到 handler 线程内的发送代理对象（M5.5 修复：此前 `QMetaObject::invokeMethod(this)` 的 `this` 是主线程亲和的 QThread 对象，导致写 socket 发生在错误线程）。

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
- 会话终止仅限本人其他会话（`terminate_session`，M5.5），被终止连接由服务端主动断开。
- **限制**：认证状态保存在 `RequestHandler` 内存中，断线重连必须重新登录；M5.5 起续期接口会真正校验携带的 token，其余命令尚未逐包验 token。

### 即时通信（M3 + M5.5 加固）

- 用户搜索、联系人关系（双向）。
- 会话模型（conversations / conversation_members）。
- 消息表（messages），服务端递增 ID；送达/已读权威记录在 `message_receipts`（按接收者/设备维度，M5.5），`messages.status` 为回执聚合出的展示值。
- 服务端实现 send_message（`clientMessageId` 幂等去重）/ ack_message（先授权再写回执）/ sync_messages（先授权再查询）/ sync_events（账号级游标同步）。
- 客户端实现会话列表、聊天窗口、消息气泡、内存 outbox（未确认消息登录成功后自动重发，幂等键保证不重复）。
- M4.5 客户端体验完善：搜索用户直接发起对话（虚拟会话 + 首条消息 ACK 后绑定 conversationId）、发送乐观显示（发送中→已发送→已送达→已读实时流转）、显式已读回执、日期分隔线、会话选中高亮与未读角标本地实时更新、侧边栏用户信息栏与登出入口、亮/暗主题切换（`Theme.qml` darkMode 驱动 + `ThemeSettings` QSettings 持久化）。
- 离线消息通过 sync_messages（afterId 游标）按会话增量同步；离线期间的消息/联系人/回执变更可经 sync_events 兜底补齐。
- 会话/消息接口全部先授权再查询（`isConversationMember()` / `canAccessMessage()`，M5.5）。
- **限制**：客户端仍无本地持久化缓存（outbox 仅在内存），重启后历史依赖重新拉取；消息撤回/删除未实现。

### 传输层安全（M5 + M5.5 fail-closed）

- 服务端 `QSslSocket` + TLS 1.2+，开发环境自签 CA（`certs/` 脚本生成）；初始化失败拒绝启动（`--allow-plaintext` 显式开发开关）。
- 客户端校验服务端证书，证书错误时断开；CA 缺失拒绝连接（`XYCHAT_ALLOW_PLAINTEXT=1` 显式开发开关）。
- 业务请求强制携带 timestamp/nonce（缺失/格式错误/超时/重复一律拒绝），nonce 由服务端全局 TTL 缓存（`NonceCache`）跨连接去重。
- 日志脱敏；敏感内存清零。
- **限制**：nonce 缓存为单服务器内存（重启清空）；消息正文对服务端可读（E2EE 属 M6）。

## 数据库 Schema（V4，M5.5 迁移）

- `schema_version`：数据库迁移版本控制
- `users`：用户基础信息（username, email, phone, password_hash）
- `devices`：设备信息（device_id, device_name, platform, public_key）——`public_key` 仅为预留列，当前无已落地的 E2EE 公钥流程
- `sessions`：登录会话（token_hash, login_ip, expires_at, last_active_at）
- `login_audit`：登录审计日志（ip_address, success, failure_reason）
- `contacts`：联系人关系（双向记录）
- `conversations`：会话信息（type, updated_at）
- `conversation_members`：会话成员（conversation_id, user_id, last_read_message_id，读游标只前进）
- `messages`：消息主体（conversation_id, sender_id, content, status, created_at, client_message_id, sender_device_id）——消息正文为服务端可读明文，E2EE 属 M6 目标
- `message_receipts`（V4 新增）：送达/已读回执（message_id, user_id, device_id, delivered_at, read_at，UNIQUE(message_id, user_id, device_id)）
- `sync_events`（V4 新增）：账号级同步事件流（seq 自增, user_id, event_type, payload），索引 (user_id, seq)

messages 表幂等唯一约束：`UNIQUE(sender_id, sender_device_id, client_message_id)`（部分索引，仅对非空幂等键生效，存量旧数据不受影响）。

## 客户端架构（M4 已落地，M4.5 完善）

```text
Chat-Client
  ├── QML UI 层（resources/）
  │     ├── main.qml（登录窗口根，objectName=loginRoot）
  │     ├── pages/（LoginPage.qml, MainPage.qml, MainWindow.qml 主窗口根，objectName=mainWindow）
  │     ├── components/（TitleBar, ConversationList, ChatView, MessageInput, MessageBubble, QWKButton）
  │     └── theme/（Theme.qml 单例，darkMode 驱动亮/暗双配色，qmldir 注册）
  ├── C++ 后端层
  │     ├── core/NetworkManager（连接状态机 + TLS + 协议，注册为 QML 上下文对象；sendMessage 返回 clientMessageId 供乐观消息跟踪）
  │     ├── core/ThemeSettings（QSettings 主题持久化，注册为 QML 上下文对象）
  │     └── models/User
  └── QWindowKit（QWK::Quick WindowAgent：无边框、拖拽、Snap Layout；标题栏自定义按钮需 setHitTestVisible 注册）
```

窗口组织（M4.5 调整）：

- `main.cpp` 依次 `engine.load()` 加载 `main.qml`（登录窗口）与 `pages/MainWindow.qml`（主窗口），两者均为独立根窗口；主窗口按 `objectName` 查找后注入登录窗口的 `mainWindow` 属性。**不能把主窗口声明在登录窗口 QML 内部**，否则会成为 transient 子窗口而不在 Windows 任务栏显示。
- 窗口流转：启动→登录窗口→（登录成功）隐藏登录窗口并显示主窗口；登出→隐藏主窗口并重新显示登录窗口；关闭主窗口退出应用，主窗口打开时关闭登录窗口仅隐藏。
- 主题：`Theme.qml` 全部颜色属性为 `darkMode ? 暗色 : 亮色` 绑定表达式，`main.qml` 用 `Binding` 将 `Theme.darkMode` 绑定到 `themeSettings.darkMode`，标题栏切换按钮写入 `themeSettings` 即全局生效并持久化。
- 聊天区：`ChatView` 消息列表直接用 `ListView`（不用外层 ScrollView 包 `height: contentHeight` 的 ListView，否则不可滚动）；自动贴底由 50ms Timer + `stayAtBottom`/`programmaticScroll` 标志实现（用户手动上滚时暂停贴底）。

与旧文档的差异说明：

- 亮/暗主题切换已于 M4.5 实现（单一 `Theme.qml` 双配色 + `ThemeSettings` 持久化），不再需要独立的 `DarkTheme.qml`/`LightTheme.qml`。

- 客户端尚无独立模型层/本地数据库；`models/User` 仅是登录态数据对象。

## 架构问题修复状态（2026-08-03 审查 → M5.5 修复）

| 级别 | 问题 | 状态与落地方式 |
| --- | --- | --- |
| P0 | 会话/消息接口缺成员授权 | ✅ 已修复：新增 `isConversationMember()` / `canAccessMessage()`，sync_messages / ack_message 先授权再查询，越权返回 `PermissionDenied` |
| P0 | `force_logout` 接受任意 `userId`，构成越权注销 | ✅ 已修复：改为 `terminate_session`，仅允许终止本人其他会话（指定他人 userId 被拒绝），被终止连接由服务端断开 |
| P0 | TLS 可静默降级 | ✅ 已修复：fail-closed（服务端拒启 / 客户端拒连），开发明文改为显式开关（`--allow-plaintext` / `XYCHAT_ALLOW_PLAINTEXT=1`） |
| P0 | timestamp/nonce 非必填，可整体绕过 | ✅ 已修复：强制必填 + 格式校验，拒绝返回 `ReplayRejected`；nonce 由服务端全局 `NonceCache`（TTL）跨连接去重 |
| P1 | 认证依赖 handler 内存状态，token 语义不完整 | ◑ 部分修复：续期接口真正校验 token；其余命令逐包验 token / channel 绑定留待后续 |
| P1 | `sendRawData` 排队写存在线程风险 | ✅ 已修复：发送投递到 handler 线程内的发送代理 QObject，socket 只在其所属线程被访问 |
| P1 | 消息无客户端幂等键，无 outbox | ✅ 已修复：`clientMessageId` + 部分唯一索引去重；客户端内存 outbox 登录成功后自动重发（本地持久化 outbox 随本地缓存一并补齐） |
| P1 | 单值 `messages.status` 无法多设备聚合 | ✅ 已修复：`message_receipts` 按接收者/设备记录，`messages.status` 改为回执聚合展示值 |
| P1 | `sync_messages` 单会话拉取 | ✅ 已补充：新增 `sync_events` 账号级游标同步（消息/联系人/回执）；sync_messages 保留为会话内历史分页 |

剩余已知问题（非阻塞）：nonce 去重为单服务器内存缓存（多服务器部署需持久化）；客户端无本地持久化缓存；服务端每连接一线程模型在高连接数下成本高；除续期外的命令未逐包验 token。

## 架构调整依据

修复方向参考主流 IM 的公开技术方案：

- **Telegram**：MTProto 以 auth_key 绑定加密通道、`random_id` 幂等去重、`getDifference/getChannelDifference` 差分同步、`sessions.killSession` 设备管理。
- **WhatsApp**：per-recipient 送达/已读回执（蓝勾模型）、客户端消息 ID 去重。
- **Signal**：预密钥（pre-key）离线密钥协商、消息级 MAC 防篡改（M6 E2EE 参考）。

共同原则：先授权再查询（authorization-before-query）、fail-closed 的传输安全、幂等写 + 游标拉（idempotent write, cursor-based pull）、推送只做通知、数据靠增量同步兜底。本项目 M5.5 修复与后续 M9 同步模型均按这些原则设计。

## 下一步演进

1. **M5.5 已完成**：上表 P0 全部修复、P1 大部分修复，并通过自动化测试（授权拒绝、nonce 拒绝/过期、幂等去重、回执聚合、读游标单调等）。
2. **M4.5 已完成**：亮/暗主题切换、CMake Widgets 残留清理、搜索发起对话、乐观发送与状态流转、已读回执、会话列表/聊天对话框交互完善，并经 E2E 验证。
3. **M6**：端到端加密一对一聊天（设备身份密钥、预密钥、消息 MAC）。
4. **M7+**：群聊、媒体、客户端本地持久化缓存与持久化 outbox、搜索与通知。
