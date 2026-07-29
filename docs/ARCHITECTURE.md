# XYChat 架构概览

## 当前组件（M3 完成后）

```text
Chat-Client ── QTcpSocket/PacketCodec/JSON ── Chat-Server ── SQLite
     │                                          │
     └──── CommonModule/encryption/protocol ───┘
```

- `Chat-Client`：Qt 桌面客户端（当前 Qt Widgets，M4 将迁移至 QML），负责登录/注册界面、主窗口、会话列表、聊天视图与网络请求。
- `Chat-Server`：Qt TCP 服务端，负责监听连接、处理注册/登录/登出/续期/消息请求、管理 session、访问 SQLite 数据库。
- `CommonModule`：客户端和服务端共享代码，提供 `EncryptionManager`（PBKDF2 慢哈希 + Token 生成）和 `Packet`/`PacketCodec` 协议层。
- `docs`：路线图、协议、安全和架构说明。
- `tests`：自动化测试入口。

## 当前能力边界

### 协议层（M1）

- 长度前缀帧协议（magic + version + messageType + requestId + payloadLength + payload）。
- 统一响应结构（code / message / data），所有请求通过 requestId 匹配。
- ping/pong 心跳与空闲超时。

### 账户体系（M2）

- 注册、登录、登出、token 续期、强制下线。
- 密码存储使用 PBKDF2-HMAC-SHA256（100,000 次迭代 + 16 字节随机盐）。
- 登录后签发 session token，服务端维护 `sessions` 表。
- 登录失败限流：同一 IP 5 分钟 10 次、同一用户 5 分钟 5 次。

### 即时通信（M3）

- 用户搜索、联系人关系（双向）。
- 会话模型（conversation / conversation_members）。
- 消息表（messages），服务端递增 ID，支持消息状态（sending/sent/delivered/read/failed）。
- 服务端实现 send_message / ack_message / sync_messages。
- 客户端实现会话列表、聊天窗口、消息气泡。
- 离线消息通过 sync_messages（afterId 游标）增量同步。

## 数据库 Schema（V3）

- `schema_version`：数据库迁移版本控制
- `users`：用户基础信息（username, email, phone, password_hash）
- `devices`：设备信息（device_id, device_name, platform, public_key）
- `sessions`：登录会话（token_hash, login_ip, expires_at, last_active_at）
- `login_audit`：登录审计日志（ip_address, success, failure_reason）
- `contacts`：联系人关系（双向记录）
- `conversations`：会话信息（type, last_message_id, updated_at）
- `conversation_members`：会话成员（conversation_id, user_id, last_read_message_id, role）
- `messages`：消息主体（conversation_id, sender_id, content, status, created_at）

## M4 架构演进：QML UI 重构

### 目标架构

```text
Chat-Client
  ├── QML UI 层（声明式界面）
  │     ├── main.qml（入口 + StackView 页面导航）
  │     ├── pages/（LoginPage, MainPage）
  │     ├── components/（TitleBar, ConversationList, ChatView, MessageInput, MessageBubble）
  │     └── theme/（Theme.qml, DarkTheme.qml, LightTheme.qml）
  ├── C++ 后端层（业务逻辑）
  │     ├── core/NetworkManager（网络通信，注册为 QML 上下文对象）
  │     └── models/（QML 数据模型适配）
  └── QWindowKit（无边框窗口框架）
        └── QWK::Quick 模块（WindowAgent 自定义标题栏、拖拽、Snap Layout）
```

### 技术选型

| 组件 | 当前（M3） | 目标（M4） |
|------|-----------|-----------|
| UI 框架 | Qt Widgets + .ui 文件 | QML + Qt Quick Controls 2 |
| 窗口框架 | 原生系统标题栏 | QWindowKit 无边框自定义窗口 |
| C++/QML 桥接 | N/A | Q_PROPERTY / Q_INVOKABLE / QQmlContext |
| 主题系统 | 无 | QML Theme 单例（亮色/暗色切换） |
| 参考设计 | 无 | Telegram Desktop 风格 |

### 关键设计决策

- **C++ 后端不变**：`NetworkManager` 等 C++ 核心逻辑保持不动，仅通过 `QQmlContext` 注册为 QML 上下文属性，QML 通过信号/槽与 C++ 交互。
- **QWindowKit 集成方式**：作为 CMake 子项目（`add_subdirectory`）或预编译库引入，使用 `QWK::Quick` 模块的 `WindowAgent` QML 类型。
- **资源管理**：QML 文件、图片、字体等通过 `resources.qrc` 打包进可执行文件。

## 下一步演进

M4 将完成客户端 UI 从 Qt Widgets 到 QML 的全面迁移，实现 Telegram 风格的现代化界面。
# XYChat 架构概览

## 当前组件

```text
Chat-Client ── QTcpSocket/PacketCodec/JSON ── Chat-Server ── SQLite
     │                                          │
     └──── CommonModule/encryption/protocol ───┘
```

- `Chat-Client`：Qt Widgets 桌面客户端，负责登录/注册界面、主窗口与网络请求。
- `Chat-Server`：Qt TCP 服务端，负责监听连接、处理注册/登录/登出/续期请求、管理 session、访问 SQLite 数据库。
- `CommonModule`：客户端和服务端共享代码，提供 `EncryptionManager`（SHA-256 摘要 + PBKDF2 慢哈希 + Token 生成）和 `Packet`/`PacketCodec` 协议层。
- `docs`：路线图、协议、安全和架构说明。
- `tests`：自动化测试入口。

## 当前边界

- 客户端与服务端通过长度前缀帧协议传输 JSON payload，已经处理粘包/拆包、协议版本和请求 ID。
- 服务端使用 SQLite，每线程独立数据库连接名。
- 密码存储使用 PBKDF2-HMAC-SHA256（100,000 次迭代 + 16 字节随机盐）。
- 登录后签发 session token，服务端维护 `sessions` 表。
- 支持注册、登录、登出、token 续期、强制下线。
- 登录失败限流：同一 IP 5 分钟 10 次、同一用户 5 分钟 5 次。

## 数据库 Schema（V2）

- `schema_version`：数据库迁移版本控制
- `users`：用户基础信息（username, email, phone, password_hash）
- `devices`：设备信息（device_id, device_name, platform, public_key）
- `sessions`：登录会话（token_hash, login_ip, expires_at, last_active_at）
- `login_audit`：登录审计日志（ip_address, success, failure_reason）

## 下一步演进

M3 将实现一对一文本聊天 MVP：消息表、发送/接收消息、离线消息同步、聊天界面。
# XYChat 架构概览

## 当前组件

```text
Chat-Client ── QTcpSocket/PacketCodec/JSON ── Chat-Server ── SQLite
     │                                          │
     └──── CommonModule/encryption/protocol ───┘
```
