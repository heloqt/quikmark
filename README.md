[简体中文](README.md) | [English](README.en.md)

# liteMark

liteMark 是一个原生、轻量、只读的 Markdown 阅读器，不依赖 Chromium 或浏览器内核，面向 AI 文档、设计文档和日常 Markdown 的快速阅读。

> 性能摘要：150 KB 文档冷启动约 150–200 ms，热打开约 20 ms，Tab 切换约 10–20 ms；10 MB 文档首屏约 180 ms，剩余内容在后台继续解析（Intel Core i5-14500、Windows 11、平衡电源模式实测）。

当前版本：**1.0.1**

## 下载

适用于 Windows 10/11 x64：

- [Gitee Releases 下载页](https://gitee.com/heloqt/litemark/releases)
- [直接下载 liteMark 1.0.1 安装包](https://gitee.com/heloqt/litemark/releases/download/v1.0.1/liteMark-1.0.1-setup.exe)

适用于 Ubuntu 22.04 及更新版本的 x86_64 桌面：

- `litemark_1.0.1_amd64.deb`：系统集成和卸载流程更完整。
- `liteMark-1.0.1-x86_64.AppImage`：单文件运行，不需要安装。

SHA256（`liteMark-1.0.1-setup.exe`）：

```text
09DD07D7C8B36F7A1BD8A7D4BA2BA1A991BF2926AC285EF28588B58BC07DC22E
```

Linux 包 SHA256：

```text
60ebfed29ae0548c436152b5de7ec832b60dce9ed15b5ffb15a6f9b2d6e09d16  litemark_1.0.1_amd64.deb
ce4a02bb7ae93bd8a84f22d5001d8392d47b31eb3320f6dae152ab9628ec3d5b  liteMark-1.0.1-x86_64.AppImage
```

Windows MSI SHA256：

```text
4F5F0E268DBF300EA9317EC7B0D8637EBCB1F4DE09D22372511D0D24426E7C90  liteMark-1.0.1-x64.msi
```

## 功能

- Windows 和 Linux 原生单窗口多 Tab 阅读器，只读，不提供编辑和保存功能。
- 支持文件菜单打开、拖入、资源管理器文件关联和第二实例路径转发。
- 读取 UTF-8 / UTF-8 BOM 文件，单个文档最大 64 MiB。
- 支持 Markdown 标题、段落、强调、删除线、列表、任务列表、引用、代码块、链接、水平线、表格和图片。
- 内置 36 个常用项目状态 PNG 图标，保留 ✅、❌、⌛、⏸️、⚠️ 等颜色；
  正文、表格、flowchart 和目录共用透明位图缓存，未收录符号回退为普通文本。
- 支持内容感知表格列宽、表头样式和超宽表格内部横向滚动。
- 本地图片、相对路径图片和 HTTP/HTTPS 网络图片异步加载。
- 支持 Mermaid flowchart 常用方向、节点、边、边标签和单层子图；不支持的内容显示源码。
- `http`、`https`、`mailto` 链接使用系统默认应用打开。
- 支持鼠标跨块选择、纯文本复制、文档内查找、匹配高亮和上下文跳转。
- 常用快捷键：`Ctrl+O` 打开、`Ctrl+R` 重新加载、`Ctrl+F` 查找、`Ctrl+W` 关闭 Tab、`Ctrl+Tab` / `Ctrl+Shift+Tab` 切换 Tab、`Ctrl+A` / `Ctrl+C` 全选和复制、`F1` 使用说明。
- 三态目录（隐藏/展开/收起）、固定宽度和分隔线三角把手，标题内部锚点和最多 5 条最近文档。
- 恢复窗口位置、Tab 顺序、当前 Tab、阅读位置和目录状态。
- 支持跟随系统、浅色和暗色主题。
- 检测文件变化并显示非模态重载提示，采用单实例运行。
- 大文档先解析安全前缀并显示首屏，完整内容在后台继续解析。
- 超长段落和超大表格自动分段，避免单个块阻塞首屏与滚动。

完整使用方式见 [使用说明](doc/12-使用说明.md)。

## 安装与卸载

Windows：

- 默认安装到当前用户目录：`%LOCALAPPDATA%\Programs\liteMark`，无需管理员权限。
- 开始菜单快捷方式始终创建；桌面快捷方式和 Markdown 文件关联可在安装组件页取消。
- 卸载时恢复安装前的 Markdown 文件关联。
- 安装目录包含 `使用说明.md`，开始菜单提供使用说明入口。
- 完成页的“运行 liteMark”默认不勾选。

Linux：

- `.deb` 可使用 `sudo apt install ./litemark_1.0.1_amd64.deb` 安装，并会部署桌面入口和 Markdown MIME 声明。
- AppImage 执行 `chmod +x liteMark-1.0.1-x86_64.AppImage` 后直接运行。
- 没有 FUSE 2 的发行版可设置 `APPIMAGE_EXTRACT_AND_RUN=1` 后运行 AppImage。
- AppImage 不会自动抢占 Markdown 默认打开方式；需要时在“文件 > 注册文件关联...”中启用。

## 构建

推荐使用 MSYS2 的 UCRT64 环境，并安装 wxWidgets 3.3、md4c、CMake 和 Ninja。

仓库中的 CMake Presets 使用 `D:\msys64-33` 作为当前开发机的工具链路径。在其他电脑上构建时，请改为本机实际的 MSYS2 UCRT64 路径，例如 `C:\msys64\ucrt64`。整个构建应使用同一套 MSYS2 根目录，不要混用不同安装目录中的头文件、静态库和 DLL。

构建 Debug 版本：

```powershell
.\scripts\build.ps1 -Preset mingw-debug
.\scripts\run.ps1 -Preset mingw-debug
```

构建可独立运行的 Release 静态版本：

```powershell
.\scripts\build.ps1 -Preset mingw-release-static
.\scripts\run.ps1 -Preset mingw-release-static
```

技术栈：C++17、wxWidgets 3.3、md4c、CMake、MinGW-w64。

## 测试

运行单元测试和 UI 回归：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test.ps1 -Preset mingw-release-static -Integration
```

性能基准：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\benchmark.ps1 -Samples 20 -LargeSamples 5
```

连续滚动压力：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\continuous-scroll-benchmark.ps1 `
  -Events 120 -IntervalMs 0
```

超大单段落和单表格压力：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\block-stress-benchmark.ps1 -Samples 5
```

性能测试脚本需要已构建的 Release 静态版，并在交互式 Windows 桌面中运行。

## 打包

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\package.ps1
```

版本从 `CMakeLists.txt` 的 `project(... VERSION ...)` 自动读取，安装器输出到 `dist\`。安装包超过 10 MB 时打包失败。

生成 Linux AppImage：

```bash
bash scripts/setup-appimage-tools.sh
bash scripts/package-appimage.sh \
  --exe build/linux-release/bin/litemark \
  --wx-lib-dir /opt/wxWidgets-3.3.0/lib
```

AppImage 会打包 wxWidgets、libcurl、GTK3、GDK-Pixbuf、图标、MIME 声明和使用说明。
当前产物面向 Ubuntu 22.04 x86_64 及兼容的新发行版。

生成无需开发环境的性能测试包：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\create-perf-kit.ps1 `
  -OutputDirectory .\dist\liteMark-perf-kit-20261023 `
  -Zip
```

## 文档

- [使用说明](doc/12-使用说明.md)
- [测试矩阵](doc/07-单元测试矩阵.md)
- [Intel Core i5-14500 性能测试记录](doc/17-Intel-14500性能测试记录.md)
- [电源模式性能对比](doc/10-电源模式性能对比.md)
- [启动阶段打点与复测](doc/14-启动阶段打点与复测.md)
- [安全软件冷启动影响测试](doc/13-安全软件冷启动影响测试.md)
- [第三方组件声明](THIRD_PARTY_NOTICES.md)

## 许可证

本项目采用 [GNU Lesser General Public License v3.0](LICENSE) 发布；第三方组件适用各自的许可条款。
