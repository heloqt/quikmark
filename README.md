[简体中文](README.md) | [English](README.en.md)

# QuikMark

QuikMark 是一个原生、轻量的 Markdown 阅读器。快速打开，专注阅读，
不提供编辑功能，不依赖 Chromium 或浏览器内核。

> 性能摘要：150 KB 文档冷启动约 150–200 ms，热打开约 20 ms，Tab 切换约 10–20 ms；10 MB 文档首屏约 180 ms，剩余内容在后台继续解析（Intel Core i5-14500、Windows 11、平衡电源模式实测）。

当前版本：**1.1.0**

## 下载

项目主页：[Gitee](https://gitee.com/heloqt/quikmark) |
[GitHub](https://github.com/heloqt/quikmark)

发布页面：[Gitee Releases](https://gitee.com/heloqt/quikmark/releases/tag/V1.1.0) |
[GitHub Releases](https://github.com/heloqt/quikmark/releases/tag/V1.1.0)

| 平台 | 安装包 | 大小 | 下载 |
| --- | --- | --- | --- |
| Windows 10/11 x64 | `quikmark-1.1.0-setup.exe` | 4.15 MiB | [Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/quikmark-1.1.0-setup.exe) / [GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/quikmark-1.1.0-setup.exe) |
| Windows 10/11 x64 | `quikmark-1.1.0-x64.msi` | 4.34 MiB | [Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/quikmark-1.1.0-x64.msi) / [GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/quikmark-1.1.0-x64.msi) |
| Ubuntu 22.04+ x64 | `quikmark_1.1.0_amd64.deb` | 3.88 MiB | [Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/quikmark_1.1.0_amd64.deb) / [GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/quikmark_1.1.0_amd64.deb) |
| Linux x86-64 | `QuikMark-1.1.0-x86_64.AppImage` | 35.95 MiB | [Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/QuikMark-1.1.0-x86_64.AppImage) / [GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/QuikMark-1.1.0-x86_64.AppImage) |

SHA256 校验文件：[Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/SHA256-1.1.0.txt) /
[GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/SHA256-1.1.0.txt)

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
- 支持鼠标跨块选择、表格单元格文字选择、纯文本复制、文档内查找、匹配高亮和上下文跳转。
- 表格支持右键“复制整个表格”，同时写入 HTML 表格和 TSV，便于粘贴到 Word、Excel 或纯文本编辑器。
- 常用快捷键：`Ctrl+O` 打开、`Ctrl+R` 重新加载、`Ctrl+F` 查找、`Ctrl+W` 关闭 Tab、`Ctrl+Tab` / `Ctrl+Shift+Tab` 切换 Tab、`Ctrl+A` / `Ctrl+C` 全选和复制、`F1` 使用说明。
- 三态目录（隐藏/展开/收起）、固定宽度和分隔线三角把手，标题内部锚点和最多 5 条最近文档。
- 恢复窗口位置、Tab 顺序、当前 Tab、阅读位置和目录状态。
- 支持跟随系统、浅色和暗色主题。
- 界面和帮助文档支持跟随系统语言、English 和简体中文，并提供独立中文语言包。
- 检测文件变化并显示非模态重载提示，采用单实例运行。
- 从 liteMark 升级时，首次启动会迁移工作区、最近文档和主题设置。
- 大文档先解析安全前缀并显示首屏，完整内容在后台继续解析。
- 超长段落和超大表格自动分段，避免单个块阻塞首屏与滚动。

完整使用方式见中文版 [使用说明](doc/12-使用说明.md) 或英文版
[Usage Guide](doc/12-Usage.md)。

## 安装与卸载

Windows：

- 默认安装到当前用户目录：`%LOCALAPPDATA%\Programs\QuikMark`，无需管理员权限。
- 开始菜单快捷方式始终创建；桌面快捷方式和 Markdown 文件关联可在安装组件页取消。
- 卸载时恢复安装前的 Markdown 文件关联。
- 安装目录包含 `使用说明.md`，开始菜单提供使用说明入口。
- 完成页的“运行 QuikMark”默认不勾选。

Linux：

- `.deb` 面向 Ubuntu 22.04 x86-64 及兼容的新发行版，可用
  `sudo apt install ./quikmark_1.1.0_amd64.deb` 安装。
- `.deb` 安装后程序入口为 `quikmark`，使用说明位于
  `/usr/share/quikmark/使用说明.md`。
- AppImage 使用前需要执行 `chmod +x QuikMark-1.1.0-x86_64.AppImage`；
  系统没有 FUSE 2 时可执行
  `APPIMAGE_EXTRACT_AND_RUN=1 ./QuikMark-1.1.0-x86_64.AppImage`。
- AppImage 不会自动抢占 Markdown 默认打开方式；需要时在
  “文件 > 注册文件关联...”中启用。

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

语言包位于 `assets/locale/`。修改中文 `.po` 文件后，使用 `msgfmt` 重新生成对应 `.mo`；详细说明见 [语言包说明](assets/locale/README.md)。

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

生成 Linux `.deb`：

```bash
bash scripts/package-deb.sh \
  --exe build/linux-release/bin/quikmark \
  --wx-lib-dir /opt/wxWidgets-3.3.0/lib
```

生成 Linux AppImage：

```bash
bash scripts/setup-appimage-tools.sh
bash scripts/package-appimage.sh \
  --exe build/linux-release/bin/quikmark \
  --wx-lib-dir /opt/wxWidgets-3.3.0/lib
```

AppImage 会打包 wxWidgets、libcurl、GTK3、GDK-Pixbuf、图标、MIME 声明和使用说明。
当前产物面向 Ubuntu 22.04 x86_64 及兼容的新发行版。

生成无需开发环境的性能测试包：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\create-perf-kit.ps1 `
  -OutputDirectory .\dist\QuikMark-perf-kit `
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
