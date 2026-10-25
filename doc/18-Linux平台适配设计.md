# Linux 平台适配设计（草案）

## 0. 文档状态

| 项目 | 内容 |
|---|---|
| 状态 | 基础平台拆分、网络图片、文件监控和 XDG 文件关联已实施 |
| 目标 | 增加原生 Linux 版本，保留现有 Windows 版本 |
| 构建方式 | Linux 原生编译，不采用 MinGW-w64 交叉编译 |
| 首轮发行 | Ubuntu 22.04 `.deb` 已验证；AppImage 后续评估 |
| 单文件 | 不作为首轮必需项；需要时优先 AppImage |

本设计与现有 [需求文档](./02-需求文档.md) 的 Windows 验收范围并行。Linux 正式纳入验收前，需要单独确认支持的发行版、最低图形环境、性能和发行包形态。

## 1. 结论

推荐采用“公共业务层 + 薄平台层”的结构：

- Markdown 解析、文档模型、布局、绘制、Tab、搜索、目录和状态恢复继续共用。
- Windows 保留注册表、WinHTTP、命名互斥量和命名管道实现。
- Linux 使用 XDG 用户目录、libcurl、Unix Domain Socket 和桌面文件，不访问注册表。
- UI 中只保留少量真正的平台条件编译；应用层和核心层不直接包含 `platform/windows/` 路径。
- 构建入口按平台选择源码，Windows 仍生成 GUI `WIN32` 可执行文件，Linux 生成普通 ELF 可执行文件。
- Linux 已完成基础功能与首轮 `.deb` 验证；AppImage 和自动升级等后续。

当前基础实施已在一台 CentOS 7.9 ECS 上完成验证：

- GCC 11、CMake 3.31.6、wxWidgets 3.3.0 和 md4c 可以完成安装。
- Windows 和 Linux 都能生成 Release 应用。
- Linux 的 16/16 个现有测试全部通过，Windows 的 14/14 个测试通过。
- Linux ELF 可在 Xvfb 中完成带 Markdown 文件的启动冒烟。
- Linux 第二实例可以向已有实例转发路径并退出。
- Linux 网络图片下载、XDG 文件关联和文件变化监控已实现并验证。
- Windows Release 可正常启动，原有单元测试通过。

## 2. 范围

### 2.1 Linux 首版必须支持

- 原生 X11/Wayland 桌面运行，优先 GTK3 后端。
- 文件菜单、命令行路径、拖入文件和多 Tab。
- UTF-8 文件、Markdown、表格、本地图片、网络图片、基础 flowchart。
- 外部链接交给系统默认应用。
- 文件变化检测和手动重新加载。
- 搜索、目录、选择复制、主题、最近文档和阅读状态恢复。
- 同一用户会话内单实例；第二次启动把路径转发给已有窗口。
- 可选、用户级的 Markdown 文件关联。

### 2.2 首轮不做

- MinGW-w64 交叉编译 Windows 版本。
- 通过 systemd 常驻、守护进程或 root 服务运行。
- 修改系统级 MIME 数据库，或要求管理员权限注册文件关联。
- macOS 适配。
- 完整替代 GTK 桌面主题。
- 强制单文件静态链接 GTK/wxWidgets。
- 立即支持所有 Linux 发行版和所有包管理器。

## 3. 目标目录与依赖方向

建议整理为：

```text
src/
  app/
    Application.cpp
  core/
    document/
    flow/
    parser/
  services/
    ImageService
  session/
  ui/
  platform/
    FileAssociation.h
    HttpClient.h
    ProcessClock.h
    SingleInstance.h
    windows/
      FileAssociation.cpp
      HttpClient.cpp
      ProcessClock.cpp
      SingleInstance.cpp
    linux/
      FileAssociation.cpp
      HttpClient.cpp
      ProcessClock.cpp
      SingleInstance.cpp
```

公共头文件只暴露标准 C++ 和 wxWidgets 类型，不出现 `HANDLE`、`HKEY`、`HWND`、socket 等平台句柄。`Application`、`MainFrame` 和核心层使用统一头文件，具体 `.cpp` 由 CMake 按目标平台选择。

依赖方向保持：

```text
app/ui/session -> services -> core
              \-> platform interfaces
platform implementations -> OS SDK / libcurl / XDG tools
```

核心层继续不依赖 wxWindow、wxDC 或平台 API。

## 4. CMake 与构建分离

### 4.1 公共工程

顶层工程只声明 C++：

```cmake
project(liteMark VERSION 1.0.0 LANGUAGES CXX)
find_package(Threads REQUIRED)
```

仅 Windows 启用 RC：

```cmake
if(WIN32)
    enable_language(RC)
endif()
```

### 4.2 源文件选择

```cmake
add_executable(litemark ${COMMON_SOURCES} ${UI_SOURCES})

if(WIN32)
    target_sources(litemark PRIVATE
        src/app/litemark.rc
        src/platform/windows/FileAssociation.cpp
        src/platform/windows/HttpClient.cpp
        src/platform/windows/ProcessClock.cpp
        src/platform/windows/SingleInstance.cpp
    )
    set_target_properties(litemark PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(litemark PRIVATE winhttp)
elseif(UNIX AND NOT APPLE)
    target_sources(litemark PRIVATE
        src/platform/linux/FileAssociation.cpp
        src/platform/linux/HttpClient.cpp
        src/platform/linux/ProcessClock.cpp
        src/platform/linux/SingleInstance.cpp
    )
    find_package(CURL REQUIRED)
    target_link_libraries(litemark PRIVATE CURL::libcurl Threads::Threads)
endif()
```

示例中的源文件列表应根据最终接口拆分调整，核心要求是：**Linux 目标中不能出现 `.rc` 或 `src/platform/windows/*`。**

### 4.3 wxWidgets 目标名兼容

wxWidgets 3.3 的不同安装可能提供两套名称：

- `wx::core`、`wx::base`
- `wxWidgets::core`、`wxWidgets::base`

CMake 应统一处理，避免把构建绑定到某个发行包：

```cmake
if(NOT TARGET wxWidgets::core AND TARGET wx::core)
    add_library(wxWidgets::core INTERFACE IMPORTED)
    set_property(TARGET wxWidgets::core PROPERTY
        INTERFACE_LINK_LIBRARIES wx::core)
endif()
if(NOT TARGET wxWidgets::base AND TARGET wx::base)
    add_library(wxWidgets::base INTERFACE IMPORTED)
    set_property(TARGET wxWidgets::base PROPERTY
        INTERFACE_LINK_LIBRARIES wx::base)
endif()
```

MD4C 的查找也应移除 `D:/msys64-33/ucrt64` 这类硬编码路径，改用标准库搜索路径或 `LITEMARK_MD4C_ROOT`。

## 5. 平台服务设计

### 5.1 单实例与文件转发

公共接口建议替代当前直接暴露的 `SingletonGuard` 和 `IpcServer` 组合：

```cpp
struct InstanceResult {
    enum class Kind { Primary, Forwarded, Failed };
    Kind kind;
};

class SingleInstance {
public:
    static std::unique_ptr<SingleInstance> Create();
    bool IsPrimary() const;
    bool StartServer(wxEvtHandler* target);
    static bool ForwardOpenRequest(
        const std::vector<wxString>& paths, int timeoutMs);
};
```

Windows 映射现有实现：

- 命名 Mutex 判断主实例。
- 命名管道接收路径。
- 次实例发送后立即退出。

Linux 推荐使用**抽象 Unix Domain Socket**：

- 名称类似 `@litemark-<uid>`。
- 第一个进程绑定成功即为主实例。
- 第二个进程连接失败后再尝试普通连接；能连接则发送路径并退出。
- socket 位于抽象命名空间，进程退出后不会留下陈旧文件。
- 服务端通过 `SO_PEERCRED` 校验客户端 UID，拒绝其他用户连接。
- 若目标桌面环境不支持抽象 socket，可回退到 `$XDG_RUNTIME_DIR/litemark-$UID.sock`，目录和 socket 权限均为当前用户。

路径协议应改为长度前缀的 UTF-8 消息，不再使用换行分隔，避免文件名中的换行符产生歧义。

### 5.2 Linux 文件关联

Linux 不使用注册表。应用内“注册文件关联”改成用户级桌面集成：

1. 写入 `$XDG_DATA_HOME/applications/litemark.desktop`。
2. 写入 `$XDG_DATA_HOME/mime/packages/litemark.xml`，声明 `*.md`、`*.markdown`。
3. 执行 `update-desktop-database` 和 `update-mime-database`。
4. 通过 `xdg-mime default litemark.desktop text/markdown` 设置默认打开方式。

所有操作只影响当前用户，不调用 `sudo`。若系统缺少 XDG 工具，菜单应显示明确错误，不影响打开文件。

接口保留能力查询：

```cpp
bool IsFileAssociationSupported();
bool RegisterLiteMarkFileAssociations(wxString* error);
bool UnregisterLiteMarkFileAssociations(wxString* error);
```

Windows 仍写 HKCU；Linux 写 XDG 文件。安装包也可以负责自动集成，但应用内的显式修复入口仍保留。

### 5.3 HTTP 网络图片

Linux 推荐用 libcurl，保留 Windows 的 WinHTTP 实现。公共接口放在 `platform/HttpClient.h`：

```cpp
struct HttpResult {
    long status = 0;
    std::vector<std::uint8_t> body;
    wxString error;
};

bool DownloadImageUrl(
    const wxString& url,
    const std::atomic_bool* cancelled,
    HttpResult* result);
```

Linux 实现约束：

- 只允许 `http` 和 `https`。
- 使用 `CURLOPT_PROTOCOLS` 与 `CURLOPT_REDIR_PROTOCOLS` 限制协议。
- 最多跟随 5 次重定向。
- 连接超时 10 秒、总超时 30 秒。
- 默认验证 TLS 证书，不提供“忽略证书错误”选项。
- 保留系统代理环境变量支持。
- 下载正文上限 64 MiB，超过上限立即中止。
- 只接受 2xx 响应。
- 通过 `CURLOPT_XFERINFOFUNCTION` 检查取消标记，Tab 关闭后及时停止下载。

Windows 实现应与 Linux 共享相同的协议、重定向、大小和取消语义；平台差异只存在于内部 API 调用。

不建议首轮引入 `wxWebRequest` 重写。虽然它本身跨平台，但会改变现有工作线程、事件回调和取消模型；libcurl 能更小地替换当前 WinHTTP 后端。后续可再评估统一迁移。

### 5.4 进程启动时间

`TraceEvent` 和 `LITEMARK_PERF_TRACE` 保持公共实现，仅把平台相关的进程时间拆出：

- Windows 继续使用 `GetProcessTimes`。
- Linux 从 `/proc/self/stat` 读取进程启动 tick，并结合系统启动时间计算进程年龄。
- Linux 的 `/proc` 时间分辨率通常为 10 ms，性能报告中需标注该限制。
- UI 内部阶段仍使用 `std::chrono::steady_clock`。

## 6. UI 平台隔离

现有 UI 大部分是 wxWidgets 绘制，可以直接复用。需要修正：

- `DocumentView.cpp`、`OutlineList.cpp`、`OutlineToggleButton.cpp` 中无条件包含的 `<wx/msw/wrapwin.h>` 改为 `#ifdef __WXMSW__`。
- `WM_COPY`、`WM_APP` 计数消息和 `BM_CLICK` 处理只在 Windows 编译。
- Linux 通过正常 wxWidgets 事件或测试专用公共方法覆盖对应测试需求。
- Windows 标题栏暗色处理保留在 `#ifdef __WXMSW__` 内。
- 字体选择改为候选列表，不再固定使用 Windows 字体。

Linux 推荐字体候选：

- 正文：`Noto Sans CJK SC`、`Source Han Sans SC`、`WenQuanYi Micro Hei`、`DejaVu Sans`
- 等宽：`JetBrains Mono`、`Cascadia Mono`、`DejaVu Sans Mono`

字体只决定优先尝试顺序，最终仍由 `wxFontEnumerator` 检查是否可用。

主题方面，liteMark 自己绘制的正文、Tab、目录和占位控件使用现有明暗配色。GTK 菜单、文件对话框和窗口装饰继续跟随桌面主题；首版不修改全局 GTK 设置。

## 7. 路径与用户数据

Linux 遵循 XDG：

| 用途 | 路径 |
|---|---|
| 设置 | `${XDG_CONFIG_HOME:-~/.config}/litemark/settings.ini` |
| 工作区状态 | `${XDG_CONFIG_HOME:-~/.config}/litemark/workspace.ini` |
| 桌面入口 | `${XDG_DATA_HOME:-~/.local/share}/applications/litemark.desktop` |
| MIME 声明 | `${XDG_DATA_HOME:-~/.local/share}/mime/packages/litemark.xml` |
| 运行时 socket 回退 | `${XDG_RUNTIME_DIR:-/tmp}/litemark-$UID.sock` |

所有 IPC 路径使用 UTF-8，文件监听继续使用 `wxFileSystemWatcher`。Linux 下它由 inotify 支撑，编辑器通过“写入临时文件再替换”保存时仍需重新注册监听。

## 8. 发行形态与单文件讨论

### 8.1 方案比较

| 方案 | 优点 | 限制 | 建议 |
|---|---|---|---|
| 目录/`.tar.gz` | 最简单，开发验证快 | 无桌面集成，需要手动解压 | 首轮测试产物 |
| `.deb` | 依赖、桌面文件、卸载流程清晰 | 首版只覆盖 Debian/Ubuntu 系 | Linux 正式主推 |
| AppImage | 单文件、可移动、用户无需安装 | 包体为几十 MB；文件关联需额外集成；部分环境依赖 FUSE | 有明确单文件需求时提供 |
| Flatpak | 沙箱和桌面集成完善 | 体积、运行环境和打包成本最高 | 暂不考虑 |
| 全静态 ELF | 表面简单 | GTK3/wxWidgets 静态链接复杂且不稳定 | 不采用 |

### 8.2 建议

Linux 首版不把单文件设为验收条件。先交付：

1. 可直接运行的构建目录。
2. 依赖明确、可复现的 `.tar.gz`。
3. 功能稳定后增加 `.deb`，由包安装桌面文件和 MIME 类型。

如果后续确实需要“下载一个文件即可运行”，优先制作 AppImage：

- 在较老 glibc 环境构建，提高兼容性。
- 打包 wxWidgets、libcurl 和必要的 GTK 运行库。
- 不承诺静态 GTK。
- 首次运行时提供“安装到应用菜单/关联 Markdown”选项。
- Linux AppImage 的包体目标单独制定，不沿用 Windows `<10 MB` 门槛。

## 9. 测试与验收

### 9.1 平台隔离测试

- Linux Release 构建中不得包含 `src/platform/windows/*`、`.rc` 或 `winhttp` 链接。
- 全仓搜索不得在无条件编译路径包含 `windows.h` 或 `<wx/msw/wrapwin.h>`。
- 公共头文件不暴露 Windows 类型。
- wx target alias 在 Linux 和 MinGW 两套 wxWidgets 安装下都能找到。

### 9.2 平台功能测试

- 第二次启动转发一个和多个文件路径。
- 同一文件已打开时只激活已有 Tab。
- 文件路径包含中文、空格、长文件名和换行符时 IPC 不截断。
- 文件被外部修改、替换和删除时，提示与监听状态正确。
- 本地图片、HTTP 图片、重定向、404、超时、64 MiB 上限和取消下载。
- HTTP 跳转到 `file://` 时必须拒绝。
- `xdg-mime query default text/markdown` 能反映用户级文件关联。
- 字体缺失时回退不能产生空白正文。
- 明暗主题切换后正文、表格、代码块和目录配色正确。

### 9.3 现有回归

- 现有 14/14 单元测试继续通过。
- Linux UI 回归覆盖打开、拖入、Tab、切换、滚动、搜索、目录、复制和外链。
- Windows 构建与性能基准不能回归。
- Linux 性能门槛单独测量；首版先记录 P50/P95，不直接套用 Windows 验收入口。

## 10. 实施阶段

| 阶段 | 内容 | 完成标准 |
|---|---|---|
| L0 | CMake 平台拆分、公共平台头文件、UI MSW 保护 | Linux 配置和编译入口成立 |
| L1 | Linux 单实例、HTTP、进程时间和文件关联 | 核心平台服务可用 |
| L2 | Linux 根目录构建和功能回归 | 可打开、切换、滚动、搜索和显示图片 |
| L3 | 文件监听、主题、字体和状态路径收口 | 日常 Linux 使用闭环 |
| L4 | `.tar.gz` 和可选 `.deb` | 可交付安装/运行包 |
| L5 | AppImage 评估 | 明确体积、桌面集成和兼容性后再决定 |

粗略工作量预计为 5–10 个开发日，主要不确定性在 GTK3 下的主题、字体和 UI 回归，而不是解析与布局核心。

## 11. 需要确认的决策

| 编号 | 待确认项 | 推荐 |
|---|---|---|
| LNX-001 | 首批支持发行版 | Ubuntu 22.04/24.04、Debian 12 x86_64 |
| LNX-002 | 发行形态 | 首轮目录/压缩包，后续 `.deb` |
| LNX-003 | 单文件要求 | 不设为首轮门槛；需要时采用 AppImage |
| LNX-004 | Linux 性能门槛 | 先基线测量，再参考 Windows 指标单独确认 |
| LNX-005 | 自动创建文件关联 | 安装包创建；便携版由用户通过菜单启用 |

本设计确认后，建议先实施 L0/L1，并保持 Windows 构建与现有测试持续通过，再进入 Linux UI 回归。

## 12. 基础版本验证记录

验证日期：2026-10-25。

### 12.1 Windows

```powershell
.\scripts\build.ps1 -Preset mingw-release-static
.\scripts\test.ps1 -Preset mingw-release-static
```

结果：

- Release 静态构建通过。
- 14/14 单元测试通过。
- 使用隔离配置目录打开 `doc/02-需求文档.md` 的启动冒烟通过。
- 完整 UI 回归通过，包括滚动恢复、跨块复制、搜索、锚点和目录。
- 媒体回归通过，本地图片和 flowchart 各加载 1 个。

### 12.2 Linux

测试实例为 CentOS 7.9 x86_64：

```bash
source /opt/rh/devtoolset-11/enable
cmake -S . -B build/linux-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/wxWidgets-3.3.0:/usr/local \
  -DLITEMARK_BUILD_TESTS=ON
cmake --build build/linux-release --parallel 1
ctest --test-dir build/linux-release --output-on-failure
```

环境：

- GCC 11.2.1
- CMake 3.31.6
- wxWidgets 3.3.0，GTK3 Unicode 共享库
- md4c 0.5.2

结果：

- 原生 x86-64 ELF Release 构建通过。
- 16/16 单元测试通过。
- Xvfb 中带 Markdown 文件完成启动冒烟。
- 第二个进程成功转发路径后退出，第一个进程继续运行。
- 网络图片下载覆盖 HTTP 成功、重定向、404 和非 HTTP 拒绝。
- XDG 文件关联测试验证桌面文件、MIME 声明和默认应用设置。
- 运行中修改 Markdown 文件后，`file_change_detected` 事件成功产生。
- 运行时依赖 wxWidgets 3.3 共享库和 GTK3。

### 12.3 当前边界

- Linux 网络图片使用 libcurl，支持 HTTP/HTTPS、重定向、超时、
  64 MiB 限制和状态码检查。
- Linux 文件关联写入用户级 XDG 桌面文件和 MIME 声明，并调用
  `xdg-mime` 设置默认应用。
- 文件变化监控在事件循环启动后延迟初始化，已验证可收到外部修改事件。
- 未生成 Linux 单文件包。

### 12.4 Ubuntu 22.04 `.deb` 验证

使用 Ubuntu 22.04.5 base rootfs 作为独立 chroot 构建和安装环境：

- 在 Ubuntu 中重新编译 wxWidgets 3.3、md4c 和 liteMark。
- 16/16 单元测试通过。
- 将 wxWidgets 的两个运行库放入 `/usr/lib/litemark`。
- 主程序 RUNPATH 设置为 `$ORIGIN/../lib/litemark`，wx 库使用 `$ORIGIN`。
- 依赖由 `dpkg-shlibdeps` 计算，覆盖 GTK3、X11、libpng、libtiff 等系统库。
- `dpkg -i` 安装成功。
- 暂时移走 `/opt/wxWidgets-3.3.0` 后，安装包仍能加载私有 wx 库并完成 Xvfb 启动冒烟。
- `dpkg -r` 卸载成功，程序和私有库均被移除。
- 在另一个全新的 Ubuntu 22.04 rootfs 中执行 `apt install ./litemark_1.0.0_amd64.deb`，依赖自动解析成功，Xvfb 启动通过，`apt purge litemark` 卸载通过。

生成的包：

```text
dist/litemark_1.0.0_amd64.deb
```

结果为 3,994,362 字节，SHA256：

```text
546061e001c23eeff2124271c6972e01ebc7f4eb71c0f5523ac79c2b176f648a
```

该包面向 Ubuntu 22.04 x86-64。包内包含 Markdown MIME 声明和桌面入口，
应用菜单会通过 XDG 写入当前用户的默认关联。
