[Simplified Chinese](README.md) | English

# liteMark

liteMark is a native, lightweight, read-only Markdown reader. It does not depend on Chromium or a browser engine, and is designed for fast reading of AI documents, design documents, and everyday Markdown files.

> Performance summary: about 150–200 ms cold start for a 150 KB document, about 20 ms to open a subsequent document, 10–20 ms for tab switching, and about 180 ms to display the first view of a 10 MB document while the remaining content is parsed in the background. Measured on an Intel Core i5-14500 running Windows 11 in Balanced power mode.

Current version: **1.0.0**

## Download

For Windows 10/11 x64:

- [Gitee Releases](https://gitee.com/heloqt/litemark/releases)
- [Download liteMark 1.0.0 installer](https://gitee.com/heloqt/litemark/releases/download/v1.0.0/liteMark-1.0.0-setup.exe)

The installer is approximately 4.04 MiB. Desktop shortcuts and `.md` / `.markdown` file associations are optional; the Start menu shortcut is always created. The "Run liteMark" option on the finish page is unchecked by default.

SHA256 (`liteMark-1.0.0-setup.exe`):

```text
3F3268AD97A421E30FBAC91E56D83185FF88DE984C3C0337AE4205E11E8EE58F
```

## Features

- Native single-window Windows reader with multiple tabs. Read-only; editing and saving are not supported.
- Open files from the File menu, drag and drop, Explorer file associations, or path forwarding from a second instance.
- Reads UTF-8 and UTF-8 BOM files, with a maximum document size of 64 MiB.
- Markdown headings, paragraphs, emphasis, strikethrough, lists, task lists, blockquotes, code blocks, links, horizontal rules, tables, and images.
- Content-aware table column widths, styled headers, and horizontal scrolling inside oversized tables.
- Asynchronous loading of local, relative-path, and HTTP/HTTPS images.
- Common Mermaid flowchart directions, nodes, edges, edge labels, and single-level subgraphs. Unsupported content is shown as source text.
- `http`, `https`, and `mailto` links open in the system default application.
- Cross-block text selection, plain-text copying, in-document search, match highlighting, and context navigation.
- Common shortcuts: `Ctrl+O` open, `Ctrl+R` reload, `Ctrl+F` find, `Ctrl+W` close tab, `Ctrl+Tab` / `Ctrl+Shift+Tab` switch tabs, `Ctrl+A` / `Ctrl+C` select all and copy, and `F1` for help.
- Three-state outline (hidden, expanded, collapsed), fixed width, draggable splitter, heading anchors, and up to five recent documents.
- Restores window position, tab order, active tab, reading position, and outline state.
- System, light, and dark themes.
- File-change detection with a non-modal reload prompt, plus single-instance operation.
- Large documents render a safe prefix first; the remaining content is parsed in the background.
- Very long paragraphs and oversized tables are split automatically to avoid blocking the first view or scrolling.

For detailed usage, see the [usage guide (Chinese)](doc/12-使用说明.md).

## Installation and Uninstallation

- Installs per user to `%LOCALAPPDATA%\Programs\liteMark`; administrator privileges are not required.
- The Start menu shortcut is always created. Desktop shortcuts and Markdown file associations can be disabled on the components page.
- Uninstallation restores the Markdown file associations that existed before installation.
- The installation directory contains a usage guide, and the Start menu provides a shortcut to it.
- The "Run liteMark" option on the finish page is unchecked by default.

## Build

MSYS2 with the UCRT64 environment is recommended. Install wxWidgets 3.3, md4c, CMake, and Ninja.

The CMake presets use `D:\msys64-33` as the toolchain path on the current development machine. On another machine, change it to the local MSYS2 UCRT64 path, such as `C:\msys64\ucrt64`. Use one MSYS2 installation consistently; do not mix headers, static libraries, or DLLs from different installations.

Build a Debug version:

```powershell
.\scripts\build.ps1 -Preset mingw-debug
.\scripts\run.ps1 -Preset mingw-debug
```

Build a standalone Release static version:

```powershell
.\scripts\build.ps1 -Preset mingw-release-static
.\scripts\run.ps1 -Preset mingw-release-static
```

Technology stack: C++17, wxWidgets 3.3, md4c, CMake, and MinGW-w64.

## Testing

Run unit tests and UI regression tests:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test.ps1 -Preset mingw-release-static -Integration
```

Performance benchmark:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\benchmark.ps1 -Samples 20 -LargeSamples 5
```

Continuous-scroll stress test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\continuous-scroll-benchmark.ps1 `
  -Events 120 -IntervalMs 0
```

Oversized paragraph and table stress test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\block-stress-benchmark.ps1 -Samples 5
```

Performance scripts require a built Release static executable and an interactive Windows desktop.

## Packaging

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\package.ps1
```

The version is read automatically from `project(... VERSION ...)` in `CMakeLists.txt`, and the installer is written to `dist\`. Packaging fails if the installer exceeds 10 MB.

## Documentation

- [Usage guide (Chinese)](doc/12-使用说明.md)
- [Unit test matrix (Chinese)](doc/07-单元测试矩阵.md)
- [Intel Core i5-14500 performance test record (Chinese)](doc/17-Intel-14500性能测试记录.md)
- [Power mode comparison (Chinese)](doc/10-电源模式性能对比.md)
- [Startup instrumentation and retest (Chinese)](doc/14-启动阶段打点与复测.md)
- [Security software cold-start impact (Chinese)](doc/13-安全软件冷启动影响测试.md)

## License

This project is released under the [GNU Lesser General Public License v3.0](LICENSE). Third-party components are governed by their respective licenses.
