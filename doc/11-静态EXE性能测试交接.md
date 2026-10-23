# 静态 EXE 性能测试交接

## 1. 交接目标

接收方只需要获得：

- 一个静态 `liteMark.exe`。
- 对应的性能测试脚本包。
- 一台 Windows 10/11 x64 测试机。

不需要安装 MinGW、CMake、wxWidgets 或 Visual C++ 运行库。静态 EXE 仍使用 Windows 自带的 UCRT、GDI/GDI+、WinHTTP 等系统 DLL。

## 2. 生成测试包

在源码工作区执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\create-perf-kit.ps1 `
  -Exe .\build\mingw-release-static\bin\liteMark.exe `
  -OutputDirectory .\dist\liteMark-perf-kit-20261022 `
  -Zip
```

生成：

```text
dist\liteMark-perf-kit-20261022.zip
```

包内包含：

```text
liteMark.exe
SHA256SUMS.txt
README.md
run-benchmark.ps1
run-power-modes.ps1
run-continuous-scroll.ps1
scripts\benchmark.ps1
scripts\compare-power-modes.ps1
scripts\continuous-scroll-benchmark.ps1
```

`benchmark.ps1` 会自动生成测试 Markdown，不使用用户自己的文档，并通过 `LITEMARK_CONFIG_DIR` 使用隔离配置，不会覆盖接收方真实 Tab、最近文档或窗口状态。

## 3. 接收方测试步骤

1. 解压完整测试包，不要只复制 exe。
2. 校验 SHA256：

```powershell
Get-FileHash .\liteMark.exe -Algorithm SHA256
Get-Content .\SHA256SUMS.txt
```

3. 连接交流电源，关闭浏览器、游戏、编译任务和同步盘。
4. Windows 电源模式设为“平衡”，显示缩放设为 100%。
5. 启动一次 `liteMark.exe` 再关闭，完成首次杀毒扫描和字体缓存预热。
6. 运行正式测试：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-benchmark.ps1 -Samples 20 -LargeSamples 5 -Name run1
```

7. 建议使用另一组样本再测一次：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-benchmark.ps1 -Samples 20 -LargeSamples 5 -Name run2
```

8. 将完整 `results` 目录压缩后回传，不需要录制整个桌面。

## 4. 可选测试

### 三档电源模式

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-power-modes.ps1 -Samples 20 -LargeSamples 5
```

脚本依次测试平衡、最佳能效、高性能。若机器无法通过 `powercfg` 切换电源模式，可手动在 Windows 设置中切换，再分别执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-benchmark.ps1 -Name balanced
```

### 连续滚动

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-continuous-scroll.ps1 -Events 120 -IntervalMs 0
```

该测试连续投递 120 个滚轮消息，并统计滚轮事件到对应帧完成绘制的 P50/P95/Max，以及超过 16.7 ms、50 ms 的帧数。

## 5. 日志目录和用途

标准测试结果位于：

```text
results\<Name>\
```

核心分析顺序：

### `performance-results.json`

首先阅读这个文件，不要先看 CSV。

| 字段 | 含义 |
|---|---|
| `Environment` | CPU、系统、内存、显示缩放、电源方案、电源模式、供电来源、Exe 路径/大小/SHA256、磁盘型号 |
| `ColdStartMs` | 150 KB 文档冷启动统计 |
| `HotOpenMs` | 150 KB 文档热打开统计 |
| `TabSwitchMs` | 普通 Tab 切换统计 |
| `ScrollMs` | 10 MB 文档独立滚动统计 |
| `PaintMs` | 绘制函数耗时统计 |
| `Large10MB.ColdStartMs` | 10 MB 冷启动挑战项 |
| `Large10MB.HotOpenMs` | 10 MB 热打开 |
| `Large10MB.TabSwitchMs` | 10 MB Tab 切换 |
| `Large10MB.ScrollMs` | 10 MB 切换后的滚动 |
| `Pass` | 普通文档硬门槛判定 |

首屏阶段拆解字段：

| 字段 | 含义 |
|---|---|
| `ColdStartBreakdown.ParseMs` | 150 KB 文件读取和 Markdown 解析 |
| `ColdStartBreakdown.FileReadMs` | 完整文件读取 |
| `ColdStartBreakdown.Utf8ConvertMs` | UTF-8 校验和必要整理 |
| `ColdStartBreakdown.PrefixMd4cParseMs` | 首屏安全前缀的 md4c 解析 |
| `ColdStartBreakdown.Md4cParseMs` | 完整文档的纯 md4c 解析 |
| `ColdStartBreakdown.NormalizeMs` | 图片、flowchart 和块 ID 后处理 |
| `ColdStartBreakdown.LayoutInitMs` | 150 KB 块槽位和高度预估初始化 |
| `ColdStartBreakdown.VisibleLayoutMs` | 150 KB 当前视口附近真实布局 |
| `ColdStartBreakdown.DrawMs` | 150 KB 当前视口块绘制 |
| `Large10MB.ColdStartBreakdown` | 10 MB 对应的四项阶段数据 |

每个统计对象包含：

```text
Samples / Min / P50 / P95 / Max
```

### 原始 CSV

CSV 格式：

```text
event,microseconds
```

| 文件 | 事件 |
|---|---|
| `perf-cold.csv` | `cold_start`、解析拆解、布局拆解和 `paint` |
| `perf-hot-switch.csv` | `hot_open`、`tab_switch`、解析拆解、布局拆解和 `paint` |
| `perf-large-cold.csv` | 10 MB 对应冷启动和阶段事件 |
| `perf-large-interactive.csv` | 10 MB `hot_open`、`tab_switch`、`scroll`、阶段事件、`paint` |
| `perf-scroll.csv` | 独立 10 MB `scroll`、`paint` |

换算：

```text
毫秒 = microseconds / 1000
```

事件边界：

- `cold_start`：进程创建到首个可读视口完成应用侧绘制。
- `hot_open`：主实例收到文件路径到首个可读视口完成绘制。
- `tab_switch`：切换操作开始到目标 Tab 完成绘制。
- `scroll`：滚轮事件进入处理到对应帧完成绘制。
- `paint`：单次文档绘制耗时。
- `parse`：文件读取、UTF-8 转换和 Markdown 解析耗时。
- `file_read`：完整文件读取耗时。
- `utf8_convert`：UTF-8 校验和字节整理耗时。
- `prefix_md4c_parse`：首屏安全前缀的 md4c 解析耗时。
- `md4c_parse`：完整文档的纯 md4c 解析耗时。
- `normalize`：图片、flowchart 和块 ID 后处理耗时。
- `layout_init`：块槽位和全文高度预估初始化耗时。
- `visible_layout`：当前视口及上下各一屏的真实布局耗时。
- `draw`：当前视口块绘制耗时。

## 6. 判定方法

普通文档当前门槛：

| 指标 | 门槛 |
|---|---:|
| 冷启动 P95 | <= 500 ms |
| 热打开 P95 | <= 300 ms |
| Tab 切换 P95 | <= 50 ms |
| 滚动 P95 | <= 16.7 ms |
| 滚动最大单帧 | <= 50 ms |

10 MB 冷启动 300 ms 是挑战目标，不参与验收失败判定。10 MB Tab 切换应单独分析；当前版本在两台机器上均可能超过 50 ms。

接收方回传后，应先比较两轮 `P50` 是否稳定，再检查 `P95/Max` 和原始 CSV 中的长尾。不能只取最好的一轮。

## 7. 常见干扰

- 第一次运行可能被杀毒软件扫描，需预热或丢弃。
- 电池模式、低电量节能和 Windows 现代待机会显著影响结果。
- 后台更新、浏览器 GPU 加速和同步盘会造成冷启动长尾。
- 外部轮询窗口标题只能说明窗口已出现，不能证明文档首屏已经可读。
- 外部“滚轮消息处理时间”若不包含绘制，不能和 `scroll` 事件直接比较。
- 不同 DPI、显示器刷新率和远程桌面会改变绘制耗时。

## 8. 回传清单

接收方需要回传：

```text
results\<run>\performance-results.json
results\<run>\perf-cold.csv
results\<run>\perf-hot-switch.csv
results\<run>\perf-large-cold.csv
results\<run>\perf-large-interactive.csv
results\<run>\perf-scroll.csv
```

若运行了电源或连续滚动测试，同时回传对应 `summary.json` 和原始 CSV。
