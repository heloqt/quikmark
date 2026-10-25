# Linux Xvfb 性能测试

## 1. 测试结论

测试日期：2026-10-25。

在 ECS 的 Xvfb 虚拟显示环境中，使用减少后的样本量完成了 Linux 基础版本测试：

| 场景 | 样本数 | P50 | P95 | 最大值 |
|---|---:|---:|---:|---:|
| 150 KB 冷启动首屏 | 5 | 108.423 ms | 113.288 ms | 113.288 ms |
| 150 KB 运行中打开 | 5 | 15.602 ms | 22.500 ms | 22.500 ms |
| 150 KB Tab 切换 | 10 | 7.076 ms | 17.517 ms | 17.517 ms |
| 10 MB 冷启动首屏 | 3 | 117.278 ms | 167.623 ms | 167.623 ms |

在这组虚拟显示环境中：

- 150 KB 冷启动 P95 为 113 ms。
- 150 KB 运行中打开 P95 为 22.5 ms。
- 150 KB Tab 切换 P95 为 17.5 ms；除首次切换外，后续大多为 6.5–8 ms。
- 10 MB 冷启动 P95 为 167.6 ms。

这些结果只是 Linux 基础版本的开发基线，不替代真实桌面的正式验收。

## 2. 测试环境

| 项目 | 值 |
|---|---|
| 平台 | CentOS 7.9 x86_64 ECS |
| CPU | Intel Xeon Platinum，2 vCPU |
| 内存 | 1.7 GiB，另有 1 GiB swap |
| 显示 | Xvfb 1280 × 800 × 24 |
| 编译器 | GCC 11.2.1 |
| GUI | wxWidgets 3.3.0，GTK3 Unicode 共享库 |
| Markdown | md4c 0.5.2 |
| 构建 | Release |
| 可执行文件 | `/opt/litemark-build/litemark-linux-src/build/linux-release/bin/litemark` |
| 文件大小 | 1,475,240 字节 |
| SHA256 | `acbd341fcc193327942694037bcb606ae40fb77d6a57ae0baf9d8f1cc50f9225` |

Xvfb 在测试前已经运行，测试期间没有启动窗口管理器或桌面合成器。

## 3. 测量口径

- 冷启动：从应用进程启动到首个可读视口完成绘制，使用应用内部的
  `cold_start` trace。
- 运行中打开：已有主进程运行，第二实例通过 Unix Domain Socket
  转发文件路径，使用主进程的 `hot_open` trace。
- Tab 切换：通过 `xdotool` 向已聚焦窗口发送
  `Ctrl+Tab`，使用应用内部的 `tab_switch` trace。
- 所有数值来自应用侧绘制完成事件，不包含截图或 Xvfb 之外的桌面合成延迟。
- 150 KB 冷启动和 hot-open 使用 5 个样本，Tab 切换使用 10 个样本，
  10 MB 冷启动使用 3 个独立进程样本。

## 4. 结果分析

### 150 KB

冷启动 P50 为 `108.423 ms`，5 个样本集中在
`105.028–113.288 ms`。其中应用自身的文件读取约 `0.196–0.229 ms`，
md4c 全文解析约 `3.18–3.48 ms`，首屏布局和绘制合计约
`10–12 ms`。主要耗时仍然来自进程装载、窗口创建和首次显示。

运行中打开 P50 为 `15.602 ms`，P95 为 `22.500 ms`。第二实例只负责
连接已有进程并转发路径，不创建第二个主窗口。

Tab 切换第一次为 `17.517 ms`，随后 9 次为
`6.496–8.880 ms`。首次切换包含目标 Tab 的首轮可见布局和绘制，
后续切换复用了已有布局缓存。

### 10 MB

冷启动 P50 为 `117.278 ms`。3 个样本中：

- 首屏前缀 md4c 解析约 `0.656–0.708 ms`。
- 完整 md4c 解析在后台继续执行，约 `249–288 ms`。
- 一次样本出现首屏后绘制长尾，使 P95 达到 `167.623 ms`。

这说明 10 MB 文档的完整解析没有阻塞首屏，同时后台解析仍会消耗 CPU
并可能影响后续绘制长尾。

## 5. 限制

- Xvfb 没有真实窗口管理器、桌面合成器或 GPU 加速，结果不能直接等同于
  GNOME/KDE 桌面环境。
- 本轮没有测量滚轮事件；Linux 版滚动、字体渲染和复杂表格仍应在真实桌面上补测。
- 样本量按本轮要求缩减，P95 只能作为初步参考。
- 当前 Linux 版本不支持网络图片下载和 Markdown 文件关联。

## 6. 复现

脚本：[scripts/perf-linux-xvfb.sh](../scripts/perf-linux-xvfb.sh)

```bash
source /opt/rh/devtoolset-11/enable
export LD_LIBRARY_PATH=/opt/wxWidgets-3.3.0/lib
./scripts/perf-linux-xvfb.sh 5 5 10 3
```

结果写入：

```text
build/linux-xvfb-performance/summary.json
build/linux-xvfb-performance/traces/cold-150kb.csv
build/linux-xvfb-performance/traces/interactive-150kb.csv
build/linux-xvfb-performance/traces/cold-10mb.csv
```
