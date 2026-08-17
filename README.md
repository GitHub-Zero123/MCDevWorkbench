# MCDevWorkbench

MCDevWorkbench 是一个面向 Minecraft 开发与调试流程的桌面日志工作站 Demo。项目基于 [MCDevLink](https://github.com/GitHub-Zero123/MCDevLink) 接收 Safaia 协议日志，并以完整的图形界面演示如何将 MCDevLink 接入自己的工具、组织会话和日志数据，以及在此基础上定制专用的日志工作站。

本项目的重点是提供可直接阅读、运行和修改的接入示例，而不是替代 MCDevLink 本身。协议连接、会话和日志事件由 MCDevLink 提供，MCDevWorkbench 负责应用生命周期、线程调度、数据整理和界面呈现。

## 当前功能

- 启动 MCDevLink Safaia 日志接收服务，并显示监听状态和本地端点。
- 展示客户端会话、连接状态、远端地址和各会话日志数量。
- 查看全部会话日志，或只查看指定会话的日志。
- 按关键字搜索日志，按全部、警告和错误级别筛选。
- 根据日志内容补充推断 `TRACE`、`DEBUG`、`INFO`、`WARN`、`ERROR` 等级。
- 支持日志自动跟随、纵向和横向滚动、单行选择及复制。
- 支持清空当前缓存的日志。
- 后台轮询 MCDevLink，收到事件后通知 UI 更新，避免网络处理阻塞界面线程。
- 对已展示日志、断开会话和待处理日志数据实施裁剪或限流，控制长时间运行时的内存占用。

## 技术组成

- C++20
- CMake 3.24+
- [MCDevLink](https://github.com/GitHub-Zero123/MCDevLink)：协议连接、会话管理和日志事件。
- [EUI-NEO](https://github.com/sudoevolve/EUI-NEO)：桌面图形界面。

两个依赖均以 Git Submodule 的形式放在 `third_party` 目录中。

## 快速开始

### 环境要求

以 Windows x64 为例，需要准备：

- 支持 C++20 的 MSVC 或 `clang-cl`
- CMake 3.24 或更高版本
- Ninja

使用 MSVC 时，请在已配置好编译环境的 Developer PowerShell 或 Developer Command Prompt 中执行命令。

### 获取源码

```powershell
git clone --recursive https://github.com/GitHub-Zero123/MCDevWorkbench.git
cd MCDevWorkbench
```

如果源码已下载但子模块尚未初始化：

```powershell
git submodule update --init --recursive
```

### 构建与运行

MSVC Debug：

```powershell
cmake --preset x64-msvc-debug
cmake --build --preset x64-msvc-debug
.\build\x64-msvc-debug\MCDevWorkbench.exe
```

MSVC Release：

```powershell
cmake --preset x64-msvc-release
cmake --build --preset x64-msvc-release
.\build\x64-msvc-release\MCDevWorkbench.exe
```

仓库还提供 `x64-clang-debug`、`x64-clang-release`、`linux-debug` 和 `macos-debug` 预设。预设只表示已有对应的 CMake 配置入口，不等同于所有平台组合均已完成验证。

## 使用方式

应用启动后会自动启动 Safaia 接收服务，标题区域会显示当前监听状态和端点。兼容 Safaia 协议的客户端建立连接并发送日志后，左侧会出现对应会话，日志区域会实时更新。

- 选择 `All sessions` 查看全部会话，或选择单个会话缩小范围。
- 在搜索框中按日志内容过滤，使用 `All`、`Warnings`、`Errors` 切换级别范围。
- 开启 `Follow` 时，日志视图会跟随最新内容。
- 单击日志行后按 `Ctrl+C` 复制该行内容。
- 使用工具栏右侧的清空按钮移除当前已缓存的日志。

## 定制自己的日志工作站

可以从以下位置开始修改：

1. 在 `src/BackendController.*` 中调整 MCDevLink 的服务配置、事件处理、日志解析、缓存策略和业务数据模型。
2. 在 `src/App.cpp` 中修改会话列表、筛选工具栏、日志表格、颜色规则和交互方式。
3. 需要接入其他协议能力时，通过 MCDevLink 的原始帧处理和发送接口扩展命令、RPC 或其他调试功能。
4. 将项目中的 `MCDevWorkbench::Backend` 作为应用侧依赖边界，减少上层界面对第三方 target 名称的直接耦合。

当前 Demo 使用 MCDevLink 的默认 `SafaiaOptions`。如果需要跨设备连接、限定目标 Minecraft 进程或调整发现范围，应在服务启动前显式配置监听地址、广播地址、发现目标或目标进程，并同步评估网络暴露风险。

## 项目结构

```text
MCDevWorkbench/
|-- assets/                  # 应用图标等资源
|-- src/
|   |-- App.cpp              # EUI-NEO 界面和交互
|   |-- BackendController.*  # MCDevLink 接入、线程和日志数据管理
|   `-- platform/            # 平台相关窗口入口与外观适配
|-- third_party/
|   |-- MCDevLink/           # 日志与调试协议后端
|   `-- EUI-NEO/             # GUI 框架
|-- CMakeLists.txt
`-- CMakePresets.json
```

## CMake 选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `MCDEVWORKBENCH_BUILD_GUI` | 桌面平台为 `ON`，Android 为 `OFF` | 是否构建 MCDevWorkbench 桌面应用 |
| `MCDEVLINK_BUILD_TESTS` | `OFF` | 是否构建 MCDevLink 单元测试 |
| `MCDEVLINK_BUILD_EXAMPLES` | `OFF` | 是否构建 MCDevLink 示例和手动探测程序 |

Android 目前只支持在关闭 `MCDEVWORKBENCH_BUILD_GUI` 时配置后端依赖；桌面 GUI 的 Android 应用胶水尚未集成。

## 使用范围与安全提示

该仓库是接入和定制示例，默认配置更适合本机或可信开发网络。当前 Safaia 链路不提供认证、授权和加密；如果准备监听非环回地址或将其用于跨设备调试，应先补充身份校验、权限控制、请求限制和传输安全机制，不应直接暴露在不可信网络中。

## 相关项目

- [MCDevLink](https://github.com/GitHub-Zero123/MCDevLink)：本项目使用的 C++ 日志与调试协议后端，也是理解接入接口和协议行为的主要参考。
- [MCDevTool](https://github.com/GitHub-Zero123/MCDevTool)：同一开发工具链下的相关项目。
