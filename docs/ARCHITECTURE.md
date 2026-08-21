# XYChat 架构概览

> 2026-08-03 依据代码审查结果重写，并于同日完成 M5.5 安全加固后再次更新；2026-08-04 完成 M4.5（M4 遗留清理与一对一聊天完善）后再次更新；2026-08-17 完成 M6（端到端加密一对一聊天，含代码审查修复）后再次更新；2026-08-21 完成 M6.5（本地持久化缓存与持久化 outbox，含代码审查修复）后再次更新。

## 当前组件（M6.5 完成后）

```text
Chat-Client ── QSslSocket/PacketCodec/JSON ── Chat-Server ── SQLite
     │        (TLS 1.2+，fail-closed)          │
     └──── CommonModule（protocol/encryption/security）────┘
```

TLS 采用 fail-closed 策略：不存在静默降级路径（服务端无证书拒启，客户端无 CA 拒连；开发明文需显式开关）。

- `Chat-Client`：Qt 桌面客户端，**UI 已全面采用 QML/Qt Quick**（M4 完成，M4.5 完善），通过 `QWindowKit::Quick` 实现无边框窗口；登录窗口与主窗口为**两个独立根窗口**（均由 `main.cpp` 经 `engine.load()` 加载，主窗口在任务栏独立显示）；C++ 后端层为 `core/NetworkManager`（网络状态机、协议编解码、TLS、M6 起集成 E2EE 引导/加密发送/接收解密/TOFU，M6.5 起接入本地缓存与持久化 outbox）、`core/KeyStorage`（M6：DPAPI 保护的本地密钥与 TOFU 指纹存储；M6.5：LocalStore 存储密钥）、`core/LocalStore`（M6.5：按账号+设备隔离的加密本地缓存）、`core/ThemeSettings`（主题偏好持久化）与 `models/User`。
- `Chat-Server`：Qt TCP 服务端，`ConnectionServer`（QTcpServer）接受连接，每连接一个 `RequestHandler`（QThread）处理注册/登录/登出/续期/联系人/消息/密钥交换请求（M6 新增 register_keys/fetch_keys），管理 session 路由与在线状态，访问 SQLite。
- `CommonModule`：客户端和服务端共享代码：
  - `protocol/`：`Packet` / `PacketCodec` 长度前缀帧协议；
  - `encryption/`：`EncryptionManager`（PBKDF2 慢哈希 + Token 生成）、`E2eeCrypto`（M6：X25519/HKDF/AES-256-GCM/envelope 编解码）；
  - `security/`：`TlsHelper`（证书生成/加载）、`LogSanitizer`（日志脱敏）、`SecureMemory`（敏感内存清零）。
- `docs`：路线图、协议、安全和架构说明。
- `tests`：Qt Test 单元测试（PacketCodec、EncryptionManager、DatabaseManager、Security、LocalStore）。

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
- 客户端实现会话列表、聊天窗口、消息气泡、持久化 outbox（M6.5：加密落库，未确认消息重启后登录成功自动重发，幂等键保证不重复）。
- M6.5 本地缓存接入：登录后立即展示上一周期的缓存会话列表，打开会话先展示本地缓存再由服务端数据覆盖；登录后基于 `sync_events` 游标自动增量同步（hasMore 自动续拉），事件写入本地缓存并推进游标。
- M4.5 客户端体验完善：搜索用户直接发起对话（虚拟会话 + 首条消息 ACK 后绑定 conversationId）、发送乐观显示（发送中→已发送→已送达→已读实时流转）、显式已读回执、日期分隔线、会话选中高亮与未读角标本地实时更新、侧边栏用户信息栏与登出入口、亮/暗主题切换（`Theme.qml` darkMode 驱动 + `ThemeSettings` QSettings 持久化）。
- 离线消息通过 sync_messages（afterId 游标）按会话增量同步；离线期间的消息/联系人/回执变更可经 sync_events 兜底补齐。
- 会话/消息接口全部先授权再查询（`isConversationMember()` / `canAccessMessage()`，M5.5）。
- **限制**：消息撤回/删除未实现；本地缓存仅供快速展示与离线查看，权威数据仍以服务端为准。

### 端到端加密（M6）

- 简化 Signal 方案：X25519 身份密钥（每设备长期）+ 一次性预密钥（客户端批量上传公钥，私钥留本地）+ 每消息临时密钥 ECDH + HKDF-SHA256 + AES-256-GCM。
- 发送链路：`sendMessage` → outbox → `fetch_keys`（服务端事务内逐设备认领预密钥）→ 逐设备加密为 envelope（另附发送方自身拷贝条目，仅身份密钥加密）→ `send_message`（服务端 fail-closed 校验后入库并同事务消费预密钥）；对方尚未注册密钥时保留 outbox 并每 30 秒重试，不丢弃。
- 接收链路：`NewMessageNotification` / `sync_messages` / `sync_events` 统一解密；按 `deviceId` 定位本机条目（自身拷贝用身份密钥解密，接收方条目逐本地预密钥试解密，GCM 标签验证），成功后删除该预密钥私钥；解密结果持久化缓存（DPAPI），重复投递/重新登录同步时由缓存兜底。
- 预密钥生命周期：认领后 10 分钟未消费自动回退；身份公钥变更时旧世代全部废弃；`fetch_keys` 连接级限流（60s/20 次）。
- 私钥存储：`KeyStorage`（AppData/e2ee，Windows DPAPI 保护，临时文件+替换原子写入）；TOFU 指纹存于 `e2ee/trust.json`，变更时 `peerIdentityChanged` 告警；解密缓存自 M6.5 起归口本地加密库 `LocalStore`（遗留 `e2ee/<account>_<device>.cache` 首次登录时自动迁入并删除，本地库不可用时回退旧文件兼容路径）。
- 产品取舍：历史消息不可恢复仅限真正丢失密钥材料的场景（更换设备/清数据）；同一设备登出重登由自身拷贝 + 解密缓存兜底；M6 前存量明文保持可读。
- **限制**：仅一对一文本消息；TOFU 无带外验证；无密钥备份/设备间迁移。

### 客户端本地加密持久化缓存（M6.5）

- `LocalStore`（AppData/localstore，SQLite，按账号+设备隔离）：会话/消息/持久化 outbox/解密缓存/sync_events 游标；消息正文与会话预览以 AES-256-GCM 加密后落库（格式 `enc1:<iv>:<密文+标签>`），磁盘上不存在可读明文。
- 存储密钥：每账号+设备随机生成 32 字节密钥，经 `KeyStorage` DPAPI 保护（`localstore/<account>_<device>.key`）；密钥无法持久化时 fail-closed 禁用缓存；密钥文件存在但 DPAPI 还原失败时拒绝启用（绝不用新密钥覆盖导致旧密文永久不可解）。
- 写入路径：发送确认（含正文）、`sync_messages`/`NewMessageNotification`/`sync_events` 解密后入库、`MessageStatusUpdate` 与回执事件更新状态（状态只前进不回退，`status_rank` 比较）；已解密正文同步写入解密缓存表，供后续 envelope 重复投递命中。
- 展示路径：登录后立即 emit 缓存会话列表（服务端响应到达后刷新，预览为占位符时先从解密缓存回填真实明文）；`syncMessages` 首页拉取先 emit 本地缓存再由服务端覆盖。
- 生命周期：登出时 `clearUserData()` 清除用户可见数据（消息/会话/outbox/同步游标）；**解密缓存与存储密钥作为 E2EE 密钥材料保留**——一次性预密钥消费后不可恢复，登出重登必须依靠解密缓存兜底（与 M6 产品承诺一致）；E2EE 身份密钥同样由 `KeyStorage` 保留复用；切换账号同样只清用户数据不毁密钥材料；`closeAndDestroy()`（删库+删密钥）仅保留给彻底销毁场景。
- **限制**：本地缓存为展示层缓存，不提供离线发送以外的完整离线能力；联系人列表仍按需从服务端拉取。

### 传输层安全（M5 + M5.5 fail-closed）

- 服务端 `QSslSocket` + TLS 1.2+，开发环境自签 CA（`certs/` 脚本生成）；初始化失败拒绝启动（`--allow-plaintext` 显式开发开关）。
- 客户端校验服务端证书，证书错误时断开；CA 缺失拒绝连接（`XYCHAT_ALLOW_PLAINTEXT=1` 显式开发开关）。
- 业务请求强制携带 timestamp/nonce（缺失/格式错误/超时/重复一律拒绝），nonce 由服务端全局 TTL 缓存（`NonceCache`）跨连接去重。
- 日志脱敏；敏感内存清零。
- **限制**：nonce 缓存为单服务器内存（重启清空）；群聊/媒体消息尚未 E2EE（M7/M8）。

## 数据库 Schema（V6，M6 迁移）

- `schema_version`：数据库迁移版本控制
- `users`：用户基础信息（username, email, phone, password_hash）
- `devices`：设备信息（device_id, device_name, platform, public_key）——`public_key` 仅为预留列，当前无已落地的 E2EE 公钥流程
- `sessions`：登录会话（token_hash, login_ip, expires_at, last_active_at）
- `login_audit`：登录审计日志（ip_address, success, failure_reason）
- `contacts`：联系人关系（双向记录）
- `conversations`：会话信息（type, updated_at）
- `conversation_members`：会话成员（conversation_id, user_id, last_read_message_id，读游标只前进）
- `messages`：消息主体（conversation_id, sender_id, content, status, created_at, client_message_id, sender_device_id）——M6 起新消息正文为 E2EE envelope 密文，存量旧消息为明文
- `message_receipts`（V4 新增）：送达/已读回执（message_id, user_id, device_id, delivered_at, read_at，UNIQUE(message_id, user_id, device_id)）
- `sync_events`（V4 新增）：账号级同步事件流（seq 自增, user_id, event_type, payload），索引 (user_id, seq)
- `device_identity_keys`（V5 新增）：设备身份公钥（user_id, device_id, identity_pub, UNIQUE(user_id, device_id)）——仅存公钥
- `prekeys`（V5 新增）：一次性预密钥公钥（user_id, device_id, pub, status: unused/claimed/used, claimed_at）——仅存公钥，认领超时回退靠 `claimed_at`（V6 迁移兼容补齐该列）

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
  │     ├── core/NetworkManager（连接状态机 + TLS + 协议，注册为 QML 上下文对象；sendMessage 返回 clientMessageId 供乐观消息跟踪；M6 起登录后自动引导 E2EE 密钥注册，发送前 fetch_keys 加密、接收后解密；M6.5 起接入 LocalStore 缓存与持久化 outbox）
  │     ├── core/KeyStorage（M6：身份/预密钥私钥持久化，Windows DPAPI 保护；TOFU 指纹存储；M6.5：LocalStore 存储密钥）
  │     ├── core/LocalStore（M6.5：按账号+设备隔离的 SQLite 加密本地缓存，见上文）
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

- 客户端自 M6.5 起具备本地数据库（`LocalStore`，仅作加密展示缓存）；`models/User` 仍是登录态数据对象，未引入独立模型层。

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

剩余已知问题（非阻塞）：nonce 去重为单服务器内存缓存（多服务器部署需持久化）；服务端每连接一线程模型在高连接数下成本高；除续期外的命令未逐包验 token。

## M6 代码审查修复记录（2026-08-17）

M6 首次实现后经代码审查发现并修复：

| 级别 | 问题 | 修复方式 |
| --- | --- | --- |
| P0 | claimed 预密钥无释放机制 + fetch_keys 无限流，可被耗尽且不自愈 | 预密钥表新增 `claimed_at`，认领 10 分钟未消费自动回退 unused；fetch_keys 连接级频率限制（60s/20 次）；消息入库与预密钥消费同事务 |
| P0 | 身份密钥轮换后旧预密钥仍可被认领，导致消息静默丢失 | `upsertIdentityKey` 检测公钥变更时废弃该设备全部 unused/claimed 预密钥（附回归测试） |
| P1 | claimPrekeys 并发竞争整体回滚 + 客户端瞬时失败即永久删除待发项 | 单设备竞争失败改为跳过；SQLite busy timeout 5 秒；客户端仅对确定性错误（3007/3005/2003）删除 outbox，瞬时错误延迟重试 |
| P1 | 预密钥补齐仅看本地计数；身份密钥损坏时发送永久阻塞 | 补齐同时参考服务端 `remainingPrekeys`；损坏的身份密钥自动重新生成并丢弃旧预密钥 |
| P2 | KeyStorage 非原子写入；DPAPI 解密中间明文未清零 | 临时文件+替换写入；中间 blob 使用后经 SecureMemory 清零 |

## M6 运行期缺陷修复记录（2026-08-20）

双客户端同机联调发现并修复：

| 问题 | 根因 | 修复方式 |
| --- | --- | --- |
| 对方未上线时发送的加密消息永久丢失 | `fetch_keys` 返回 AccountNotFound/KeyBundleUnavailable 时客户端将消息从 outbox 删除，而对方尚未注册密钥属可恢复状态 | 保留 outbox，按目标用户 30 秒退避重试，对方首次登录注册密钥后自动送达；仅 CannotSendToSelf 才删除 |
| 登出重登后自己发出的消息无法解密 | envelope 只含对方设备条目，发送方本机无密文拷贝 | 发送时追加自身拷贝条目（`prekeyId=0`，仅身份密钥加密，不消费预密钥）；服务端校验放行发送方设备的该类条目（同机 deviceId 相同时与接收方条目分开去重） |
| 登出重登后对方发来的消息无法解密 | 一次性预密钥解密后即删除，内存解密缓存随登出清空 | 解密缓存按账号+设备持久化（DPAPI 保护，`e2ee/<account>_<device>.cache`），登录后加载；预密钥删除后重新同步由缓存兜底 |
| 中间版本数据库兼容 | 早期构建创建的 prekeys 表可能缺 `claimed_at` 列 | 新增 V6 迁移补齐该列 |

回归测试：新增 `selfCopyEnvelopeRoundTrip`（prekeyId=0 条目编解码与仅身份密钥加解密往返）；4 组测试套件全部通过。

## M6.5 本地持久化实施与审查修复记录（2026-08-21）

新增 `LocalStore` 与持久化 outbox，实现后经代码审查发现并修复：

| 级别 | 问题 | 修复方式 |
| --- | --- | --- |
| 警告 | 服务端会话列表的占位预览 `[Encrypted message]` 覆盖本地已解密预览，缓存预览被架空 | upsert 前用本地解密缓存按 `lastMessageId` 回填真实明文（UI 同步受益） |
| 警告 | 存储密钥不区分“文件缺失”与“DPAPI 还原失败”，瞬时失败会用新密钥覆盖致整库永久不可解 | 密钥文件存在但还原失败时拒绝启用缓存（fail-closed），仅文件缺失时生成新密钥 |
| 建议 | upsert 无条件覆盖 status，滞后同步可能回退已读状态 | 新增 `status_rank` 列，状态只前进不回退（附回归测试 `statusOnlyMovesForward`） |

另修复的构建期缺陷：`QJsonValue::toString()` 对缺失字段返回 null QString，Qt SQLite 驱动将其绑定为 SQL NULL 导致 NOT NULL 约束失败——统一规范化为非 null 空串。自动化测试：新增 TestLocalStore（落盘密文不可读、outbox 持久化、upsert 语义、遗留迁移、登出销毁等），5 组测试套件全部通过。

## M6.5 运行期缺陷修复记录（2026-08-21 联调）

双客户端互发消息后登出重登，对方消息无法解密且 UI 显示 envelope 密文原文。两个叠加缺陷：

| 问题 | 根因 | 修复方式 |
| --- | --- | --- |
| 登出重登后对方消息永久无法解密 | 登出时 `closeAndDestroy()` 整库销毁（含 decrypt_cache）并删存储密钥，而 M6 的“登出重登靠持久化解密缓存兜底”前提是缓存跨越登出存活；一次性预密钥已消费不可恢复 | 新增 `clearUserData()`：登出/切换账号只清用户可见数据（消息/会话/outbox/游标），解密缓存与存储密钥作为 E2EE 密钥材料保留 |
| envelope 密文伪装成正文显示 | 解密失败时 content 残留 envelope 原文（仅靠 undecryptable 标志），`upsertMessage` 将其当明文加密落库，缓存先行展示时又无标志 | `upsertMessage` 对 undecryptable 或 `looksLikeEnvelope` 的 content 一律清空并标记；新增 `healEnvelopeLeaks()` 打开时自愈历史污染行 |

回归测试：新增 `logoutClearsUserDataButKeepsDecryptCache`、`upsertNeverPersistsEnvelopeCiphertext`、`healsLegacyEnvelopeLeakRows`；另修复同类的空密文 null 绑定问题（content_enc/last_message_enc）。5 组测试套件全部通过。

## 架构调整依据

修复方向参考主流 IM 的公开技术方案：

- **Telegram**：MTProto 以 auth_key 绑定加密通道、`random_id` 幂等去重、`getDifference/getChannelDifference` 差分同步、`sessions.killSession` 设备管理。
- **WhatsApp**：per-recipient 送达/已读回执（蓝勾模型）、客户端消息 ID 去重。
- **Signal**：预密钥（pre-key）离线密钥协商、消息级 MAC 防篡改（M6 E2EE 参考）。

共同原则：先授权再查询（authorization-before-query）、fail-closed 的传输安全、幂等写 + 游标拉（idempotent write, cursor-based pull）、推送只做通知、数据靠增量同步兜底。本项目 M5.5 修复与后续 M9 同步模型均按这些原则设计。

## 下一步演进

1. **M5.5 已完成**：上表 P0 全部修复、P1 大部分修复，并通过自动化测试（授权拒绝、nonce 拒绝/过期、幂等去重、回执聚合、读游标单调等）。
2. **M4.5 已完成**：亮/暗主题切换、CMake Widgets 残留清理、搜索发起对话、乐观发送与状态流转、已读回执、会话列表/聊天对话框交互完善，并经 E2E 验证。
3. **M6 已完成**：端到端加密一对一聊天（X25519 身份密钥/预密钥、每消息临时密钥、AES-GCM 认证加密、envelope fail-closed、TOFU、历史消息不可恢复），含审查后修复（见上表）。
4. **M6.5 已完成**：客户端本地加密持久化缓存与持久化 outbox（`LocalStore`），重启后历史消息即刻可见、未发送消息不丢失，登出清除本地数据，含审查后修复（见上表）。
5. **M7+**：群聊（M7a 明文群聊 → M7b Sender Keys 群 E2EE）、媒体、搜索与通知、设备信任带外验证与密钥备份策略。
