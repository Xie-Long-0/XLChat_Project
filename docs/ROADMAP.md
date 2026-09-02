# XYChat 长期实现路线图

本文档面向 Qt/C++ Client + Server 基础框架，目标是逐步演进为一个"类 Telegram"的安全即时通信系统。路线图按"先稳定基础，再做通信能力，再做安全与规模化"的顺序推进，便于长期迭代、验收和回滚。

> 本文档于 2026-09-02 完全重构：已完成里程碑压缩为能力摘要（逐项勾选清单与实施流水账不再保留，历史细节经 `git log` 与 `docs/ARCHITECTURE.md` 追溯）；新增集中管理的欠账清单；M8-M11 规划按代码现状重写；原头部 11 条更新块与原 Sprint 看板合并入文末"变更记录"表。

## 0. 文档定位与维护约定

- **状态标注**：里程碑状态取 `已完成` / `未开始` / `进行中`；节内未实施项以"未实现"文字标注，不使用悬挂的空复选框。
- **更新方式**：里程碑完成时同步更新四处——① 状态总表；② 已完成能力摘要；③ 欠账清单（新增或销账）；④ 变更记录表。**禁止**再向文档头部追加流水账式更新引用块。
- **一致性要求**：涉及协议/安全/架构事实的表述必须与 `docs/PROTOCOL.md`、`docs/SECURITY.md`、`docs/ARCHITECTURE.md` 及代码一致；发现文档与代码不符时，以代码为准并在当期修正文档。
- **完成定义**：见第 9 节；里程碑勾选"已完成"前必须通过对应自动化测试与代码审查。

## 1. 项目现状总览（截至 2026-09-02）

### 1.1 里程碑状态总表

| 里程碑 | 名称 | 状态 | 完成日期 | 交付摘要 |
| --- | --- | --- | --- | --- |
| M0 | 工程基线与可维护性 | 已完成 | 2026-07-01 | README/构建说明、`.gitignore`、`docs/` 四文档、CI、Qt Test 引入、CMake 工程统一 |
| M1 | 网络协议层重构 | 已完成 | 2026-07-01 | `Packet`/`PacketCodec` 长度前缀帧协议、requestId 匹配、统一错误码、ping/pong 心跳与空闲超时 |
| M2 | 账户体系与认证安全 | 已完成 | 2026-07-29 | 注册、PBKDF2-HMAC-SHA256 密码存储、session token、多设备管理、登录限流、版本化数据库迁移 |
| M3 | 一对一文本聊天 MVP | 已完成 | 2026-07-29 | 用户搜索/联系人、会话模型、消息收发/状态/离线同步接口、客户端聊天界面 |
| M4 | 客户端 QML UI 重构 | 已完成 | 2026-07-29 | QWindowKit 无边框窗口、Telegram 风格 QML 全套页面组件、NetworkManager QML 适配 |
| M4.5 | M4 遗留清理与聊天完善 | 已完成 | 2026-08-04 | 亮/暗主题切换、搜索直接发起对话、乐观发送、显式已读回执，及 8 项 E2E 验证期缺陷修复 |
| M5 | 传输层加密与会话安全 | 已完成 | 2026-08-03 | TLS 1.2+（QSslSocket）、重放保护字段、日志脱敏、安全内存 |
| M5.5 | 安全加固（审查修复） | 已完成 | 2026-08-03 | TLS fail-closed、timestamp/nonce 强制 + 全局 TTL 去重、会话/消息先授权再查询、`clientMessageId` 幂等、per-recipient 回执模型、账号级 `sync_events` 游标 |
| M6 | 端到端加密一对一聊天 | 已完成 | 2026-08-17 | 简化 Signal 方案：X25519 身份密钥 + 一次性预密钥 + 每消息临时密钥 ECDH + HKDF-SHA256 + AES-256-GCM envelope；服务端 fail-closed 只存密文；TOFU（2026-08-20 追加修复离线发送丢失与重登解密两项联调缺陷） |
| M6.5 | 本地持久化缓存与 outbox | 已完成 | 2026-08-21 | `LocalStore` 按账号+设备隔离的加密本地库（会话/消息/持久化 outbox/解密缓存/同步游标），缓存先行展示 + 游标增量同步 |
| M7a | 明文群聊 | 已完成 | 2026-08-21 | 群管理五接口（建群/邀请/退群自动转让/踢人层级保护/群信息）、群消息 fan-out + sync_events 兜底、系统消息与群变更通知、按人数回执聚合、客户端群聊 UI（2026-08-22 热修复联调崩溃：QML 会话列表差分更新、移除 add 动画、LocalStore 连接自愈） |
| M7b | 群聊端到端加密（Sender Keys） | 已完成 | 2026-09-02 | 每发送方每群独立 chain key + Ed25519 签名，HKDF ratchet 派生消息密钥，AES-256-GCM 加密；sender-key 经 M6 pairwise E2EE 分发；`fetch_group_keys`（类型 71/72）；服务端群 envelope fail-closed 校验；DoS 上限防护（`MaxRatchetSteps=2000`/`MaxMessageIteration=1e8`）；双客户端联调通过 |
| M8 | 媒体、文件与对象存储 | 未开始 | — | 见第 4.1 节 |
| M9 | 多端同步与离线一致性 | 未开始 | — | 范围已按现状收缩，见第 4.2 节 |
| M10 | 搜索、通知与体验完善 | 未开始 | — | 见第 4.3 节 |
| M11 | 稳定性、可观测性与运维 | 未开始 | — | 登录限流已在 M2 落地，密钥拉取连接级限流已在 M6/M7b 落地，其余见第 4.4 节 |

### 1.2 能力矩阵

| 能力域 | 现状 |
| --- | --- |
| 账户与认证 | 注册/登录/登出/token 续期/`terminate_session`（仅本人其他会话）；PBKDF2 密码存储；登录失败限流（IP 5min/10 次、用户 5min/5 次）；多设备识别（`deviceId` 取自机器唯一 ID）。**未实现**：逐包验 token、`validateSession()` 回查 DB、双因素认证、注销/找回 |
| 一对一聊天 | E2EE（envelope 密文，服务端 fail-closed）、`clientMessageId` 幂等、乐观发送 UI、per-recipient 回执（delivered/read）、消息状态实时推送、离线 outbox（加密持久化，跨重启重发） |
| 群聊 | 建群/邀请/退群（群主自动转让）/踢人（角色层级保护）/群信息；群 E2EE（Sender Keys，服务端只见密文）；系统消息（成员变更胶囊渲染）；小群直推 fan-out + sync_events 兜底；按接收用户人数聚合的送达/已读计数。**未实现**：成员变更密钥 healing 与失权回收、大群拉取模式、改群名接口（数据层已就绪） |
| 本地存储 | `LocalStore`（SQLite，按账号+设备隔离）：消息/会话预览/outbox/解密缓存 AES-256-GCM 加密落库，存储密钥 DPAPI 保护；M7b 起含 `sender_keys` 表；登出清用户可见数据、保留密钥材料 |
| 多端同步 | 账号级 `sync_events` 事件流（message/receipt/contact_added/group_changed）+ 设备本地游标，登录后缓存先行 + 增量拉取（hasMore 自动续拉）。**未实现**：已读状态向已读者自身其他设备同步、服务端事件保留清理、编辑/删除/置顶/静音 |
| 传输安全 | TLS 1.2+ fail-closed（服务端无证书拒启、客户端无 CA 拒连，开发明文需显式开关）；重放保护（timestamp ±300s + nonce 全局 TTL 600s 去重）；日志脱敏（LogSanitizer） |
| 客户端 UI | QML/Qt Quick + QWindowKit 无边框双窗口（登录/主窗口独立）；Telegram 风格主题（亮/暗切换持久化）；群聊三对话框（建群/群信息/邀请）；群 E2EE 状态横幅 |

## 2. 已完成能力摘要

各里程碑的目标、关键交付、验证证据与已知限制。实施细节（逐项任务清单、审查修复过程）经 `git log` 与 `docs/ARCHITECTURE.md` 追溯。

### M0：工程基线（2026-07-01）

- 交付：README（构建/运行/依赖/目录）、`.gitignore`、`docs/` 架构/协议/安全/路线图四文档、GitHub Actions CI（configure/build/test）、Qt Test 框架与 `TestEncryptionManager`、顶层 CMake 统一（C++20、警告选项、`XYCHAT_BUILD_TESTS` 开关）。
- 验证：新开发者按 README 可构建启动；CI 全流程通过。

### M1：网络协议层（2026-07-01）

- 交付：`CommonModule/protocol` 长度前缀帧协议（magic `XYCP` + version + messageType + requestId + payloadLength，payload 上限 4 MiB）；客户端连接状态机（未连接/连接中/已连接/登录中/已认证/断线重连）；服务端连续包处理；ping/pong 心跳与 90 秒空闲超时。
- 验证：`TestPacketCodec`（连续 1000 小包、大包分片到达）。

### M2：账户体系（2026-07-29）

- 交付：注册（用户名主标识，邮箱/手机可选）；PBKDF2-HMAC-SHA256（100K 迭代 + 16B 随机盐 + 参数版本 `v1:`）；session token（服务端只存 SHA-256 摘要，7 天有效期）；`users`/`devices`/`sessions`/`login_audit` 表拆分；版本化迁移机制（`schema_version`）；登录限流；token 续期与 `terminate_session`。
- 验证：`TestDatabaseManager`（注册/session/审计/迁移）、`TestEncryptionManager`（PBKDF2/常数时间比较）。

### M3：一对一聊天 MVP（2026-07-29）

- 交付：用户搜索、双向联系人；`conversations`/`conversation_members`/`messages` 模型；`send_message`/`ack_message`/`sync_messages`；客户端会话列表、聊天窗口、消息气泡、五态消息状态；服务端递增消息 ID。本地缓存项当时未实施，由 M6.5 承接落地。
- 验证：`TestDatabaseManager` 消息/会话用例；双客户端实时收发与离线补收人工验证。

### M4 + M4.5：QML UI（2026-07-29 / 2026-08-04）

- 交付：QWindowKit 无边框窗口（自定义标题栏/拖拽/Snap Layout）；`LoginPage`/`MainPage`/`ConversationList`/`ChatView`/`MessageInput`/`MessageBubble`/`TitleBar` 组件；`Theme.qml` darkMode 双配色 + `ThemeSettings` 持久化；旧 Widgets UI 删除。M4.5 补齐：搜索直接发起对话（虚拟会话 + 首条消息 ACK 后绑定）、乐观发送、显式已读回执、日期分隔线、未读角标本地更新、登出入口；验证期修复 8 项缺陷（delegate 渲染空白、滚动/贴底、气泡自适应、头像色绑定、主窗口任务栏显示等）。
- 验证：qmllint；M1-M3 功能在 QML 下回归通过；E2E 人工验证。

### M5 + M5.5：传输安全与加固（2026-08-03）

- 交付：TLS 1.2+（开发自签 CA 自动生成，SAN localhost/127.0.0.1）；fail-closed（无静默降级路径，开发明文需 `--allow-plaintext`/`XYCHAT_ALLOW_PLAINTEXT=1` 显式开关）；timestamp/nonce 强制必填 + 全局 `NonceCache`（TTL 600s、上限 10 万条、跨连接）；会话/消息接口先授权再查询（`isConversationMember`/`canAccessMessage`，越权 3006）；`force_logout` 改 `terminate_session`（仅本人会话）；handler 线程内发送代理（消除跨线程写 socket）；`clientMessageId` 幂等键 + 部分唯一索引 + 内存 outbox；`message_receipts` 回执表 + 成员读游标（只前进）；`sync_events` 账号级游标接口。
- 验证：`TestSecurity`（nonce 系列）、`TestDatabaseManager`（越权拒绝/幂等去重/回执聚合/读游标单调/sync_events 游标）。
- 已知限制：逐包验 token 未实施；nonce 缓存单服务器内存态；端到端 TLS 集成测试缺失。

### M6：一对一 E2EE（2026-08-17，08-20 联调修复）

- 交付：每设备 X25519 身份密钥 + 批量一次性预密钥（`register_keys`/`fetch_keys`，服务端只存公钥）；每消息临时密钥 ECDH + HKDF-SHA256（salt `xychat-e2ee-v1`）+ AES-256-GCM envelope（逐设备条目 + 发送方自身拷贝 `prekeyId=0`）；服务端 fail-closed（非法/明文正文拒绝入库，3008；入库与预密钥消费同事务）；预密钥生命周期（认领即消费、10 分钟超时回退、身份变更废弃旧世代、`fetch_keys` 连接级限流 60s/20 次）；TOFU 指纹 + 变更告警；私钥 `KeyStorage`（Windows DPAPI，临时文件+替换原子写入）；解密缓存持久化。产品决策：历史消息不可恢复（仅限丢失密钥材料场景），UI 显示"无法解密此消息"。
- 验证：`TestEncryptionManager`（原语/envelope/协商全流程）、`TestDatabaseManager`（密钥管理）；双客户端联调（08-20 修复：对方未注册密钥时 outbox 保留重试不丢弃；自身拷贝 + 持久化解密缓存解决登出重登解密）。
- 已知限制：TOFU 无带外验证；无密钥备份/设备间迁移；非 Windows 平台私钥明文回退。

### M6.5：本地持久化缓存（2026-08-21）

- 交付：`LocalStore`（AppData/localstore，`<username>_<deviceId>.db`）：消息/会话/持久化 outbox/解密缓存（归口替代 M6 `.cache` 文件，遗留自动迁入）/sync_events 游标；全部正文 AES-256-GCM 加密落库（存储密钥随机生成、DPAPI 保护、加密失败拒写 fail-closed）；登录后缓存先行展示 + 游标增量同步（hasMore 自动续拉）；登出清用户可见数据、保留解密缓存与存储密钥（E2EE 密钥材料，重登解密兜底）；连接失效自愈（`ensureUsableDb()` 重开，失败则禁用缓存）。
- 验证：`TestLocalStore`（磁盘字节级密文校验、outbox 幂等、登出语义、群字段）。

### M7a：明文群聊（2026-08-21，08-22 热修复）

- 交付：协议消息类型 60-70 与错误码 3009-3012；数据库 V7（`conversations.name` + `conversation_members.role`）；服务端五处理器（建群：创建者 owner、成员上限 200；邀请：单批 ≤100、已在群拒绝；退群：群主自动转让最早入群成员；踢人：owner 可移除 admin/member、admin 仅 member；群信息：仅成员）；`send_message` 按 `conversationId`/`toUserId` 分流，群消息 fan-out（在线直推 + 全员 sync_events 兜底）；成员变更系统消息（`contentType=system`）与 `GroupChangedNotification`/`group_changed` 事件；回执按接收用户人数聚合（`receiptUserCount` 多设备去重，`MessageStatusUpdate` 携带 deliveredCount/readCount）。客户端：群组五接口 + 群消息 outbox 分流、LocalStore 群字段、建群/群信息/邀请三对话框、群样式会话列表、系统消息胶囊、"暂未端到端加密"横幅（M7b 后改为已加密提示）。
- 验证：`TestDatabaseManager` 群组 8 用例；qmllint 零错误；08-22 热修复联调崩溃（QML delegate 悬空通知端点，见变更记录）后双客户端联调通过。
- 已知限制：大群拉取模式未实现；改群名接口未开放（`setGroupName` 数据层就绪）。

### M7b：群聊 E2EE（2026-09-02 提交）

- 交付：`CommonModule/encryption/GroupE2eeCrypto`（简化 Signal Sender Keys）——每发送方每群独立 `SenderKey`（32B chain key + Ed25519 签名密钥对，`keyId` = SHA-256(签名公钥) hex 前 32 字符）；chain key 经 HKDF-SHA256 ratchet（salt `xychat-grp-chain`）派生消息密钥；群消息 AES-256-GCM 加密 + Ed25519 签名（覆盖 `iv || ciphertext`）；群消息 envelope（`contentType=e2ee_group`）含 `keyId`/`iteration`/`senderDeviceId`；sender-key 分发（`contentType=sender_key_distribution`）复用 M6 pairwise E2EE 逐设备加密 chain key（base64）；服务端 `fetch_group_keys`（类型 71/72）一次性返回全群成员密钥包（共享 fetch_keys 限流窗口）；服务端对两类群正文 fail-closed 校验（非法返回 3008）；客户端 `LocalStore.sender_keys` 表加密保存 chain key/签名密钥对/迭代数，登出保留；分发消息只处理不展示不落库。安全修复：ratchet DoS 上限（`MaxRatchetSteps=2000`、`MaxMessageIteration=1e8`）。
- 验证：`TestGroupE2eeCrypto` 20 用例（原语/ratchet/篡改与回滚拒绝/DoS 上限/envelope 编解码/fail-closed）；`tests/e2e/TestGroupRepro` 双客户端全链路（建群→分发→加密收发→登出重登→再发）退出码 0；`ctest` 6/6 通过；M7a 验收标准一并经联调确认。
- 已知限制：成员加入/退出的密钥 healing 与失权成员回收未实现（新成员需发送方手动重新分发或重新登录触发；被移除成员未被轮换出局）。

## 3. 已知欠账与风险清单

集中管理所有已识别但未实施的修复/功能项；销账或新增时更新本表（优先级 P1 最高）。

| 优先级 | 类别 | 条目 | 来源 | 影响/说明 |
| --- | --- | --- | --- | --- |
| P1 | 安全 | `RequestHandler::validateSession()` 仅查内存态（`m_currentSessionId`/`m_authenticatedUserId`），不回查 `sessions` 表 | 2026-09-02 周度审查 | token 被 `terminate_session`/过期/登出后，存量连接在其生命周期内仍可能通过校验；修复方向：逐请求回查 DB（带短 TTL 缓存）或结合逐包验 token |
| P1 | 安全 | 除续期外命令未逐包验 token / TLS channel 绑定 | M5.5 遗留 | 认证依赖连接级内存状态，断线重连必须重新登录；与上一条同根源，宜一并设计 |
| P1 | 功能 | 群成员变更 Sender-Key healing 与失权回收 | M7b 遗留 | 新成员可能收不到既有发送方密钥（需手动重分发/重登触发）；被移除成员保留旧 chain key（缺乏后向安全）；需设计成员变更触发的重分发与轮换 |
| P2 | 工程 | `sync_events` 无保留清理机制 | M9 盘点 | 事件表无限增长；清理需保证落后设备可回退全量拉取（`sync_messages`/`get_conversations`）不破坏历史 |
| P2 | 功能 | 已读状态多端同步缺失 | M9 盘点 | receipt 聚合事件只写发送方事件流；已读者自身其他设备无事件源，未读数/已读态不同步 |
| P2 | 工程 | 发消息/搜索限流未实施 | M11 前置项（2026-08-21 曾建议随 M6.5/M7 落地，未实施） | `send_message`（私聊/群聊）与 `search_users` 无每用户频率限制 |
| P2 | 工程 | 结构化日志未实施 | M11 前置项（同上） | 现为分散 `qDebug`/`qWarning` 文本日志，缺请求 ID/用户/设备/错误码/耗时的结构化字段（LogSanitizer 脱敏已在用） |
| P2 | 工程 | 端到端 TLS 集成测试缺失 | M5.5 遗留 | `tests/e2e/TestGroupRepro` 为手动工具（不纳入 CTest，需手动启动服务端），无自动化 TLS 双端集成测试 |
| P2 | 安全 | nonce 去重为单服务器内存态 | M5.5 | 服务端重启清空；多服务器部署需持久化/共享存储 |
| P2 | 安全 | TOFU 无带外验证；无密钥备份/设备间迁移 | M6 | 首次通信无法抵抗服务端中间人；更换设备/清数据后历史消息不可恢复（产品已决策接受） |
| P2 | 安全 | 非 Windows 平台私钥/存储密钥明文回退 | M6/M6.5 | DPAPI 仅 Windows；Linux/macOS 部署需接平台密钥环（libsecret/Keychain） |
| P3 | 功能 | 消息编辑/删除/撤回、会话置顶/免打扰 | M9 规划 | 全新特性栈（协议 + 迁移 + 服务端 + 客户端 + UI），需单独立项 |
| P3 | 功能 | 大群拉取/游标模式；改群名接口 | M7a 遗留 | 当前仅小群直推；`setGroupName` 数据层就绪、接口层未开放 |
| P3 | 功能 | 桌面通知；简化图片消息 | M6.5 提前项（未实施） | 分别归属 M10/M8 完整实现 |
| P3 | 工程 | `GroupE2eeCrypto.cpp` 使用 `QStringLiteral`，违反项目代码风格约定 | 2026-09-02 文档重构盘点 | 风格不一致（项目约定禁用该宏）；随下次触碰该文件的代码任务顺手修正 |

## 4. 未来里程碑规划

### 4.1 M8：媒体、文件与对象存储（4-8 周）

- **目标**：支持图片、语音、视频和文件消息。
- **依赖**：M3/M7a 消息通道（已完成）；媒体 E2EE 依赖 M6/M7b 加密基础（已完成）。
- **任务**：
  - 文件上传协议：分片、校验、断点续传。
  - 服务端文件元数据表（`files`）与对象存储接口。
  - 图片缩略图、视频封面、语音时长。
  - 客户端上传/下载进度、失败重试、取消。
  - 大文件不走消息 TCP 主通道，使用独立 HTTP(S) 上传下载服务。
  - 文件内容客户端加密后上传（复用 envelope/Sender-Key 体系）。
- **验收标准**：
  - 发送 1MB 图片和 100MB 文件稳定成功。
  - 断网后恢复可续传。
  - 客户端能清理缓存并重新下载。

### 4.2 M9：多端同步与离线一致性（2-4 周，范围按现状收缩）

- **目标**：同一账号多设备登录时，会话、消息、已读状态保持一致。
- **依赖**：M5.5 `sync_events`（已完成）、M6.5 本地缓存与游标（已完成）。
- **已提前落地（不在本里程碑范围）**：
  - 账号级全局同步序列号与设备游标（M5.5/M6.5）。
  - 消息/联系人/回执/群变更统一 `sync_event` 抽象（M5.5/M7a）。
  - 客户端启动后先增量同步再进入实时（M6.5：`openLocalStore` 缓存先行展示 + `syncEvents(cursor)` 增量拉取 + hasMore 自动续拉）。
- **剩余任务**：
  - 已读状态多端同步：成员在设备 A 已读后，其设备 B 实时同步（现状缺口：`ack_message(read)` 仅向发送方推送/写事件；方案方向：向已读者自身 `sync_events` 追加 `read_cursor` 类事件，客户端 ingest 后更新本地未读角标与消息状态）。
  - `sync_events` 保留清理：保留期/容量策略 + 落后于清理点的设备回退全量拉取，不破坏历史（对应欠账 P2）。
  - 冲突处理及配套特性：消息编辑/删除、会话置顶/免打扰——均为全新特性栈，**建议单独立项分批实施**（先置顶/免打扰，后编辑/删除），编辑/删除需纳入 `sync_events` 事件类型与幂等语义设计。
- **验收标准**：
  - 设备 A 已读消息后，同账号设备 B 同步为已读（未读角标与消息状态一致）。
  - 离线 24 小时后上线只增量同步缺失事件；清理事件后的落后设备可回退全量拉取且不丢历史。
  - 服务端可清理过期 `sync_events` 而不破坏 `sync_messages` 历史拉取。
  - （特性栈立项后）编辑/删除/置顶/静音在多端间一致。

### 4.3 M10：搜索、通知与体验完善（4-6 周）

- **目标**：把 MVP 从"能用"提升到"好用"。
- **依赖**：M6.5 本地缓存（本地消息搜索的数据底座，已完成）。
- **任务**：
  - 客户端本地消息搜索（基于 LocalStore，注意密文列需经解密缓存/索引设计）。
  - 服务端联系人/用户名搜索增强（现有 `search_users` 基础上补分页/模糊度控制）。
  - 桌面通知、声音、未读角标完整配置（系统托盘、通知点击定位会话）。
  - 草稿、表情基础能力。
  - 会话置顶、免打扰、删除会话（与 M9 特性栈协同立项，避免重复设计）。
  - 国际化与主题系统扩展。
- **验收标准**：
  - 用户能快速找到联系人、会话和历史消息。
  - 通知行为符合系统习惯且可配置。

### 4.4 M11：稳定性、可观测性与运维（持续）

- **目标**：为真实用户使用做好稳定性基础。
- **已提前落地**：登录限流（M2）；`fetch_keys`/`fetch_group_keys` 连接级限流（M6/M7b）。
- **说明**：结构化日志与发消息/搜索限流曾于 2026-08-21 建议前置到 M6.5/M7 期间，未随期实施，现列为独立欠账（见第 3 节 P2），可按第 5 节顺序提前落地，不必等 M11 整体启动。
- **任务**：
  - 结构化日志：请求 ID、用户 ID、设备 ID、错误码、耗时（配合 LogSanitizer 脱敏）。
  - 指标监控：在线连接数、消息吞吐、失败率、延迟、数据库慢查询。
  - 崩溃捕获与客户端日志上报。
  - 限流：发消息、搜索、文件上传（登录限流已有）。
  - 备份与恢复演练。
  - 压力测试：长连接数、消息吞吐、离线同步峰值。
- **验收标准**：
  - 能回答"当前多少在线用户、消息延迟多少、失败率多少"。
  - 服务端异常重启后不丢已确认消息。
  - 压测报告可指导扩容。

## 5. 推荐执行顺序（2026-09-02 重排）

下一步候选按"安全欠账优先、横切能力其次、特性栈分批"排序；**具体下一任务待讨论确定**：

1. **`validateSession()` 回查 DB 修复**（P1 安全欠账，改动小，可单独实施或与后续任一项合并）。
2. **M11 前置两项：发消息/搜索限流 + 结构化日志**（服务端横切能力，越早落地后续功能越早受益）。
3. **M9 核心一致性：已读状态多端同步 + `sync_events` 保留清理**（收掉 M9 三条基础验收）。
4. **M7b healing：成员变更 Sender-Key 重分发与失权回收**（群 E2EE 安全闭环）。
5. **M9 特性栈：置顶/免打扰 → 编辑/删除**（单独立项，分批实施）。
6. **M8 媒体文件**。
7. **M10 搜索/通知/体验**。

## 6. 目录结构（2026-09-02 与实际仓库同步）

```text
XYChat_Project/
  3rdparty/               # 预编译依赖：QWindowKit、OpenSSL、zlib（include/lib/bin/src）
  CommonModule/           # 客户端/服务端共享模块
    protocol/             # Packet/PacketCodec（消息类型 1-72，错误码 1000-9002）
    encryption/           # EncryptionManager/E2eeCrypto(M6)/GroupE2eeCrypto(M7b)
    security/             # LogSanitizer/SecureMemory/TlsHelper
  Chat-Client/
    core/                 # NetworkManager/KeyStorage/LocalStore/ThemeSettings
    models/               # 数据模型
    resources/
      pages/              # LoginPage/MainPage/MainWindow
      components/         # TitleBar/ConversationList/ChatView/MessageInput/MessageBubble/QWKButton
      theme/              # Theme.qml（darkMode 双配色）
      icons/
      main.qml
      resources.qrc
    main.cpp
  Chat-Server/
    core/                 # Server/RequestHandler/NonceCache
    database/             # DatabaseManager 与迁移（当前 V7）
    main.cpp
  docs/                   # ARCHITECTURE/PROTOCOL/ROADMAP/SECURITY
  tests/
    unit/                 # TestPacketCodec/TestEncryptionManager/TestDatabaseManager/
                          # TestSecurity/TestLocalStore/TestGroupE2eeCrypto（均纳入 CTest）
    e2e/                  # TestGroupRepro（双客户端群 E2EE 复现，手动运行，不纳入 CTest）
  certs/                  # 开发证书生成脚本（运行时证书自动生成于可执行文件同级 certs/）
```

## 7. 数据库演进

- **服务端**（SQLite，版本化迁移，当前 V7）：`schema_version`、`users`、`devices`、`sessions`、`login_audit`、`contacts`、`conversations`（V7 增 `name`）、`conversation_members`（V7 增 `role`）、`messages`、`message_receipts`、`sync_events`、`device_identity_keys`（M6，仅公钥）、`prekeys`（M6，仅公钥）。中长期若需多人并发/多实例部署，迁移 PostgreSQL/MySQL，并尽早抽象 Repository/DAO。
- **客户端 LocalStore**（SQLite，按账号+设备隔离，正文加密落库）：`schema_meta`、`messages`、`conversations`（含群名/成员数）、`outbox`（含 `conversation_id`）、`decrypt_cache`、`meta`（同步游标）、`sender_keys`（M7b：chain key/Ed25519 签名密钥对/迭代数，登出保留）。

## 8. 安全注意事项

- 不要把 SHA-256 当作密码存储方案；它太快，不适合抵抗离线撞库。
- 不要自己设计未经验证的密码学协议；优先参考成熟方案和库。
- 不要在日志中输出密码、token、私钥、验证码、完整密文密钥材料。
- 不要把服务端能解密的"普通加密聊天"宣传成端到端加密。
- 端到端加密需要明确密钥验证、设备更换和历史消息恢复策略。
- 对所有外部输入做长度限制、格式校验和速率限制。
- ratchet/循环类解密路径必须设步数与参数上限（M7b DoS 教训：恶意 `iteration` 可迫使接收端长时间运算）。

## 9. 每个迭代的完成定义

每个功能迭代都应同时交付：

- 协议文档更新（`docs/PROTOCOL.md`）。
- 数据库迁移脚本或 schema 变更说明。
- 服务端处理逻辑。
- 客户端调用与 UI。
- 单元测试或集成测试。
- 错误码和日志。
- 安全影响说明（`docs/SECURITY.md`）。
- 本路线图状态同步（状态总表 + 能力摘要 + 欠账清单 + 变更记录）。

## 10. 不建议现在立刻做的事

- 不建议一开始就做超大规模分布式架构；先把单机可靠性做好。
- 不建议过早引入复杂微服务；当前模块化单体更适合快速迭代。
- 不建议在协议未稳定时做过度复杂的 UI 动效和装饰（M7a 热修复教训：模型高频搅动 + 过渡动画曾致 delegate 悬空崩溃）。
- 不建议自行发明完整端到端加密协议；应在充分调研后实现。
- 不建议把文件传输塞进主聊天长连接；大文件应走独立上传下载通道。

## 11. 变更记录

原头部流水账更新块与原 Sprint 看板（Sprint 1-6，均已完成）合并为本表；详细过程经 `git log` 追溯。

| 日期 | 事件 | 摘要 |
| --- | --- | --- |
| 2026-07-01 | M0/M1 完成 | 工程基线与协议层重构落地 |
| 2026-07-29 | M2/M3/M4 完成 | 账户体系、一对一聊天 MVP、QML UI 重构落地 |
| 2026-08-03 | 审查更正 + M5/M5.5 完成 | 更正 M3/M4/M5 中被提前标记完成的条目；新增并完成 M5.5 安全加固（fail-closed/nonce 强制/授权/幂等/回执/sync_events） |
| 2026-08-04 | M4.5 完成 | 亮暗主题、搜索发起对话、乐观发送、已读回执及 8 项验证期缺陷修复 |
| 2026-08-17 | M6 完成 | 一对一 E2EE（简化 Signal），审查修复预密钥泄漏/耗尽、身份轮换静默丢消息 |
| 2026-08-20 | M6 联调修复 | 离线发送保留重试不丢弃；自身拷贝条目 + 持久化解密缓存解决登出重登解密；V6 迁移 |
| 2026-08-21 | 路线图调整 + M6.5/M7a 完成 | 新增 M6.5；M7 拆 M7a/M7b；M9 范围收缩；M6.5 落地；M7a 三子任务（协议与数据模型/服务端处理器与 fan-out/客户端 UI）落地 |
| 2026-08-22 | M7a.3 热修复 + M7b 实现 | 修复群聊联调崩溃（WER 定位 QML delegate 悬空通知端点：会话列表 clear+全量重建改差分更新、移除消息列表 add 动画、LocalStore 连接自愈与驱动检查，提交 `bafd4f6`）；M7b Sender-Key 群 E2EE 实现并双客户端联调通过（代码随 2026-09-02 安全修复后一并提交 `c806d90`） |
| 2026-08-26 | 安全审查 | 发现 GroupE2eeCrypto ratchet 循环无上限（DoS）、服务端群消息缺 envelope fail-closed 校验、`validateSession()` 仅查内存态三项问题 |
| 2026-09-02 | 安全修复 + M7b 入库 + 文档重构 | DoS 上限（`MaxRatchetSteps`/`MaxMessageIteration`）与服务端群 envelope fail-closed 落地（`TestGroupE2eeCrypto` 扩至 20 用例）；群聊横幅改为"已启用端到端加密"；M7b 连同修复提交（`c806d90`）；周度审查确认欠账清单；ROADMAP 完全重构（本版本），`validateSession()` 修复仍待实施 |
