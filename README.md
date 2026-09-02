# XYChat Project

XYChat 是一个基于 Qt 6 / C++20 的即时通讯原型项目，当前包含桌面客户端 `Chat-Client` 与 TCP 服务端 `Chat-Server`。已具备账户体系、TLS 传输安全、一对一聊天端到端加密（M6）、群聊与群聊端到端加密（M7a/M7b，Sender Keys 方案，服务端仅存储密文）、本地加密缓存与增量同步等能力。本仓库现阶段的目标是提供稳定的工程基线，后续按 `docs/ROADMAP.md` 逐步演进协议、认证、消息与安全能力。

## 环境要求

| 组件 | 版本/要求 |
| --- | --- |
| CMake | 3.21 或更高版本 |
| C++ 编译器 | 支持 C++20；Windows 推荐 MSVC 2022，Linux/macOS 可使用 GCC/Clang |
| Qt | Qt 6.8.3 |
| OpenSSL | OpenSSL 3.x |
| 目标平台 | 当前以 Windows + MSVC 2022 为主要开发平台；CMake 工程保留跨平台构建能力 |

> 仓库的 `3rdparty/` 目录包含 Windows 开发用的 OpenSSL 相关文件。其他平台建议通过系统包管理器安装 OpenSSL，并通过 `CMAKE_PREFIX_PATH` 指向安装目录。

## 目录说明

```text
.
├── Chat-Client/          # Qt QML 客户端：登录/主窗口、NetworkManager、KeyStorage、LocalStore（M6.5 本地加密缓存）
├── Chat-Server/          # Qt Core/Network/Sql 服务端：TCP 监听、请求处理、SQLite
├── CommonModule/         # 客户端与服务端共用模块：协议编解码、加密（PBKDF2/E2EE）、安全工具
├── docs/                 # 架构、协议、安全与路线图文档
├── tests/                # 自动化测试
├── 3rdparty/             # Windows 第三方依赖文件
└── CMakeLists.txt        # 顶层 CMake 工程
```

## 构建

### Windows（MSVC 2022 + Qt 6.8.3）

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
```

### Linux/macOS（Qt 已安装在自定义路径时）

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build -j
```

如需跳过测试目标，可在配置时传入 `-DXYCHAT_BUILD_TESTS=OFF`。

## 运行

先启动服务端：

```bash
./out/build/release/Chat-Server
```

再启动客户端：

```bash
./out/build/release/Chat-Client
```

> 客户端自 M6.5 起会在系统 AppData 目录下维护按账号+设备隔离的本地加密缓存（消息/会话以 AES-256-GCM 加密落库，未发送消息跨重启保留）；登出时自动清除。详见 `docs/SECURITY.md` 的本地存储安全章节。

## 测试

配置并构建后运行全部单元测试（CTest 纳入 6 套）：

```bash
ctest --test-dir build --output-on-failure
```

| 套件 | 覆盖范围 |
| --- | --- |
| `TestPacketCodec` | 帧协议编解码 |
| `TestEncryptionManager` | PBKDF2 / Token 生成 |
| `TestDatabaseManager` | 服务端数据层（含群组与 V1-V7 迁移） |
| `TestSecurity` | TLS 辅助 / 日志脱敏 / NonceCache 重放保护 |
| `TestLocalStore` | 客户端本地加密缓存与持久化 outbox |
| `TestGroupE2eeCrypto` | 群 Sender-Key 加密原语（M7b） |

另有 `tests/e2e/TestGroupRepro`：双客户端群 E2EE 端到端复现工具，**不纳入 CTest**，需先启动 `Chat-Server` 后手动运行：

```bash
./build/tests/TestGroupRepro
```

## 开发约定

- C++ 标准统一为 C++20。
- 文件编码统一为 UTF-8，换行符统一为 CRLF。
- CMake 目标按客户端、服务端、公共模块、测试分层组织。
- 新协议或安全行为变更需要同步更新 `docs/PROTOCOL.md`、`docs/SECURITY.md` 与 `docs/ROADMAP.md`。
- 不要提交构建目录、SQLite 数据库、临时日志、IDE 用户文件或本地 CMake preset。

### C++ 代码风格（适用于所有源文件）

- **注释格式**：禁止在注释中使用长横线分隔线（如 `──────────────`）或长破折号（如 `——`）作为段落/区块分隔符；区分代码区块使用简洁的短注释标题（例如 `// 文本加解密`，而非 `// ── 文本加解密 ─────────`）。
- **字符串字面量**：禁止使用 `QStringLiteral(...)` 宏包装常量字符串，统一使用双引号字面量（`const char*` 或依赖隐式转换）；需要 `QString` 类型的场景（如三元表达式、字面量成员调用）直接使用 `QString("...")`。
