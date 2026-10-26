[Simplified Chinese](README.md) | English

# QuikMark

QuikMark is a native, lightweight Markdown reader built for fast, focused
reading. It does not depend on Chromium or a browser engine and intentionally
does not provide editing or saving.

> Performance summary: a 150 KB document cold-starts in roughly 150–200 ms,
> opens as a subsequent document in about 20 ms, and switches tabs in about
> 10–20 ms. A 10 MB document displays its first view in about 180 ms while the
> remaining content is parsed in the background. Measured on an Intel
> Core i5-14500 running Windows 11 in Balanced power mode.

Current version: **1.1.0**

The application interface and bundled usage guide are currently in Simplified
Chinese. English UI localization is planned for a future release.

## Download

Project home:
[Gitee](https://gitee.com/heloqt/quikmark) |
[GitHub](https://github.com/heloqt/quikmark)

Releases:
[Gitee Releases](https://gitee.com/heloqt/quikmark/releases/tag/V1.1.0) |
[GitHub Releases](https://github.com/heloqt/quikmark/releases/tag/V1.1.0)

| Platform | Package | Size | Download |
| --- | --- | --- | --- |
| Windows 10/11 x64 | `quikmark-1.1.0-setup.exe` | 4.15 MiB | [Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/quikmark-1.1.0-setup.exe) / [GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/quikmark-1.1.0-setup.exe) |
| Windows 10/11 x64 | `quikmark-1.1.0-x64.msi` | 4.34 MiB | [Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/quikmark-1.1.0-x64.msi) / [GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/quikmark-1.1.0-x64.msi) |
| Ubuntu 22.04+ x64 | `quikmark_1.1.0_amd64.deb` | 3.88 MiB | [Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/quikmark_1.1.0_amd64.deb) / [GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/quikmark_1.1.0_amd64.deb) |
| Linux x86-64 | `QuikMark-1.1.0-x86_64.AppImage` | 35.95 MiB | [Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/QuikMark-1.1.0-x86_64.AppImage) / [GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/QuikMark-1.1.0-x86_64.AppImage) |

SHA256 checksums:
[Gitee](https://gitee.com/heloqt/quikmark/releases/download/V1.1.0/SHA256-1.1.0.txt) /
[GitHub](https://github.com/heloqt/quikmark/releases/download/V1.1.0/SHA256-1.1.0.txt)

## Features

- Native single-window, multi-tab reader for Windows and Linux. Read-only,
  with no editing or saving support.
- Open files from the File menu, drag and drop, Explorer or Linux desktop file
  associations, and path forwarding to an existing single-instance window.
- Reads UTF-8 and UTF-8 BOM documents up to 64 MiB.
- Markdown headings, paragraphs, emphasis, strikethrough, lists, task lists,
  blockquotes, fenced code blocks, links, horizontal rules, tables, and
  images.
- 36 embedded PNG status icons preserve the colors of common symbols such as
  ✅, ❌, ⌛, ⏸️, and ⚠️. Document text, tables, flowcharts, and the outline
  share a transparent bitmap cache; unsupported symbols fall back to text.
- Content-aware table column widths, styled headers, and horizontal scrolling
  inside oversized tables.
- Asynchronous loading of local, relative-path, and HTTP/HTTPS images.
- Common Mermaid flowchart directions, nodes, edges, edge labels, and
  single-level subgraphs. Unsupported content is displayed as source text.
- `http`, `https`, and `mailto` links open in the system default application.
- Cross-block text selection, plain-text copying, in-document search, match
  highlighting, and context navigation.
- Shortcuts: `Ctrl+O` open, `Ctrl+R` reload, `Ctrl+F` find, `Ctrl+W` close tab,
  `Ctrl+Tab` / `Ctrl+Shift+Tab` switch tabs, `Ctrl+A` / `Ctrl+C` select all and
  copy, and `F1` for help.
- Three-state outline (hidden, expanded, or collapsed), fixed width, a splitter
  handle, heading anchors, and up to five recent documents.
- Restores window position, tab order, active tab, reading position, outline
  state, theme, and recent documents.
- System, light, and dark themes.
- File-change detection with a non-modal reload prompt and single-instance
  operation.
- Migrates workspace, recent-document, and theme settings from liteMark on the
  first launch.
- Large documents display a safely parsed prefix first while the full document
  is parsed in the background.
- Very long paragraphs and oversized tables are split automatically to avoid
  blocking the first view or scrolling.

For detailed usage, see the [usage guide (Chinese)](doc/12-使用说明.md).

## Installation and Uninstallation

### Windows

- Installs per user to `%LOCALAPPDATA%\Programs\QuikMark`; administrator
  privileges are not required.
- The Start menu shortcut is always created. The desktop shortcut and
  Markdown file associations can be disabled on the components page.
- Uninstallation restores the Markdown file associations that existed before
  installation.
- The installation directory contains `使用说明.md`, and the Start menu links
  to it.
- The "Run QuikMark" option on the finish page is unchecked by default.

### Linux

The `.deb` package targets Ubuntu 22.04 x86-64 and compatible newer
distributions:

```bash
sudo apt install ./quikmark_1.1.0_amd64.deb
```

- After installation, the executable is `quikmark`, and the usage guide is at
  `/usr/share/quikmark/使用说明.md`.
- Make the AppImage executable before running it:

  ```bash
  chmod +x QuikMark-1.1.0-x86_64.AppImage
  ./QuikMark-1.1.0-x86_64.AppImage
  ```

- If FUSE 2 is unavailable, run the AppImage with:

  ```bash
  APPIMAGE_EXTRACT_AND_RUN=1 ./QuikMark-1.1.0-x86_64.AppImage
  ```

- AppImage does not automatically become the default Markdown application.
  Use **File > Register File Associations...** when desired.

## Build

MSYS2 with the UCRT64 environment is recommended on Windows. Install
wxWidgets 3.3, md4c, CMake, and Ninja.

The CMake presets use `D:\msys64-33` as the toolchain path on the current
development machine. On another machine, change it to the local MSYS2 UCRT64
path, such as `C:\msys64\ucrt64`. Use one MSYS2 installation consistently; do
not mix headers, static libraries, or DLLs from different installations.

Build a Debug version:

```powershell
.\scripts\build.ps1 -Preset mingw-debug
.\scripts\run.ps1 -Preset mingw-debug
```

Build a standalone Release version with static wxWidgets libraries:

```powershell
.\scripts\build.ps1 -Preset mingw-release-static
.\scripts\run.ps1 -Preset mingw-release-static
```

Technology stack: C++17, wxWidgets 3.3, md4c, libcurl, CMake, and MinGW-w64.

## Testing

Run unit tests and UI regression tests:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test.ps1 -Preset mingw-release-static -Integration
```

Run the performance benchmark:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\benchmark.ps1 -Samples 20 -LargeSamples 5
```

Run the continuous-scroll stress test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\continuous-scroll-benchmark.ps1 `
  -Events 120 -IntervalMs 0
```

Run the oversized-paragraph and table stress test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\block-stress-benchmark.ps1 -Samples 5
```

Performance scripts require a built Release executable and an interactive
Windows desktop.

## Packaging

Build the Windows installer:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\package.ps1
```

The version is read automatically from `project(... VERSION ...)` in
`CMakeLists.txt`, and the installer is written to `dist\`. Packaging fails if
the installer exceeds 10 MiB.

Build the Debian package on Linux:

```bash
bash scripts/package-deb.sh \
  --exe build/linux-release/bin/quikmark \
  --wx-lib-dir /opt/wxWidgets-3.3.0/lib
```

Build the AppImage on Linux:

```bash
bash scripts/setup-appimage-tools.sh
bash scripts/package-appimage.sh \
  --exe build/linux-release/bin/quikmark \
  --wx-lib-dir /opt/wxWidgets-3.3.0/lib
```

The AppImage bundles wxWidgets, libcurl, GTK3, GDK-Pixbuf, the application
icon, MIME definitions, and the usage guide. The current binary targets Ubuntu
22.04 x86-64 and compatible newer distributions.

## Documentation

- [Usage guide (Chinese)](doc/12-使用说明.md)
- [Test matrix (Chinese)](doc/07-单元测试矩阵.md)
- [Intel Core i5-14500 performance test record (Chinese)](doc/17-Intel-14500性能测试记录.md)
- [Power mode comparison (Chinese)](doc/10-电源模式性能对比.md)
- [Startup instrumentation and retest (Chinese)](doc/14-启动阶段打点与复测.md)
- [Security software cold-start impact (Chinese)](doc/13-安全软件冷启动影响测试.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## License

This project is released under the
[GNU Lesser General Public License v3.0](LICENSE). Third-party components are
governed by their respective licenses.
