# liteMark

liteMark 是一个原生、轻量、只读的 Markdown 阅读器，目标是不依赖浏览器内核，提供快速打开、Tab 切换和干净阅读体验。

当前仓库处于工程骨架阶段：

- wxWidgets 3.3 + C++17 + CMake + MinGW-w64。
- 单窗口多文档 Tab。
- 文件菜单、文件关联参数和拖入打开。
- UTF-8/UTF-8 BOM 读取及 64 MiB 安全上限。
- 跟随系统、浅色和深色外观。
- 纯文本预览占位，后续替换为 Markdown Block 渲染器。

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
