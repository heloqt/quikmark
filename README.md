# liteMark

liteMark 是一个原生、轻量、只读的 Markdown 阅读器，目标是不依赖浏览器内核，提供快速打开、Tab 切换和干净阅读体验。

当前仓库处于工程骨架阶段：

- wxWidgets 3.3 + C++17 + CMake + MinGW-w64。
- 单窗口多文档 Tab。
- 文件菜单、文件关联参数和拖入打开。
- UTF-8/UTF-8 BOM 读取及 64 MiB 安全上限。
- 跟随系统、浅色和深色外观。
- 基于 md4c 的块级 Markdown 解析和自绘渲染。
- 标题、段落、强调、链接、图片占位、列表、引用、代码块和基础表格。
- 表格使用内容感知列宽、轻量分隔线和表头强调；超宽表格支持内部横向滚动。
- 支持 `Ctrl + 鼠标滚轮` 调整正文字体大小（75%–200%，每格 10%）。
- `http`/`https` 外部链接使用系统默认浏览器打开，`mailto` 使用默认邮件应用。
- 本地及 HTTP/HTTPS 网络图片支持 PNG、JPEG、BMP 和 GIF 首帧；相对路径以 Markdown 文件目录为基准，网络图片异步下载。
- Mermaid flowchart 支持常用方向、矩形/圆角/菱形/圆形节点、常用连线、边标签和单层子图；不支持时回退源码。
- 首次打开立即建立 Tab，文件读取和 Markdown 解析在后台线程执行。
- 只对可视区和一屏预取内容做精确排版，其余块使用估算高度。
- 每份文档的布局缓存，Tab 切换时复用已完成的块布局。
- 单实例窗口和命名管道文件转发。
- 文件变化目录监听、Tab 标记和非模态重新加载提示。
- Windows 安装器及 `.md`/`.markdown` 文件关联。

需求与设计文档位于 [`doc/`](doc/)。

## 构建环境

本项目的独立工具链位于：

```text
D:\msys64-33
```

使用 `D:\msys64-33\ucrt64`，不要与现有 `D:\msys64` 混用。

## 构建

在 PowerShell 中执行：

```powershell
.\scripts\build.ps1 -Preset mingw-debug
.\scripts\run.ps1 -Preset mingw-debug
```

Release 静态 wxWidgets 构建：

```powershell
.\scripts\build.ps1 -Preset mingw-release-static
.\scripts\run.ps1 -Preset mingw-release-static
```

Debug 版本依赖 `D:\msys64-33\ucrt64\bin` 中的 wxWidgets DLL，请使用
`run.ps1` 启动。Release 静态版本可以独立启动。

命令行可以直接传入 Markdown 路径；`--smoke-test` 用于无交互启动检查。

## 打包

```powershell
.\scripts\package.ps1 -Version 1.0.0
```

安装器输出到 `dist\`，打包脚本会拒绝超过 10 MB 的制品。

## 测试

```powershell
ctest --test-dir .\build\mingw-debug --output-on-failure
```
