# liteMark 静态 EXE 性能测试包

## 适用环境

- Windows 10/11 x64。
- 无需安装 MinGW、CMake、wxWidgets 或其他运行库。
- PowerShell 5.1 或更高版本。
- 测试时连接交流电源，关闭其他高负载程序。

## 快速测试

1. 解压整个目录。
2. 双击或在 PowerShell 中进入该目录。
3. 运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-benchmark.ps1 -Samples 20 -LargeSamples 5
```

完成后把 `results` 目录压缩后回传。

## 测试内容

- 约 150 KB 文档冷启动 20 次。
- 约 150 KB 文档热打开 20 次。
- 约 150 KB 文档 Tab 切换 40 次。
- 10 MB 文档冷启动 5 次。
- 10 MB 文档热打开 5 次。
- 10 MB 文档 Tab 切换 20 次。
- 10 MB 文档滚动 30 次。

## 推荐测试流程

1. 在 Windows 设置中把电源模式设为“平衡”。
2. 显示器缩放设为 100%。
3. 先运行一次快速预热：

```powershell
.\liteMark.exe
```

关闭窗口后，再运行正式测试。
4. 使用默认参数运行 `run-benchmark.ps1`。
5. 建议再运行一次，确认冷启动长尾是否稳定：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-benchmark.ps1 -Samples 20 -LargeSamples 5 -Name run2
```

## 电源模式对比

如系统允许通过 `powercfg` 修改电源模式：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-power-modes.ps1
```

如果提示无法切换电源模式，请手动在 Windows 设置中选择平衡/高性能/最佳能效，然后分别运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-benchmark.ps1 -Name balanced
```

## 连续滚动压力

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\run-continuous-scroll.ps1
```

默认向 150 KB 文档连续投递 120 个滚轮消息。

## 应回传的文件

回传整个 `results` 目录。最重要的是：

- `performance-results.json`：P50/P95/Max 汇总。
- `perf-cold.csv`：冷启动原始事件。
- `perf-hot-switch.csv`：热打开与 Tab 切换原始事件。
- `perf-large-cold.csv`：10 MB 冷启动。
- `perf-large-interactive.csv`：10 MB 热打开、切换和滚动。
- `perf-scroll.csv`：10 MB 独立滚动。

## 日志阅读方式

- CSV 每行格式为 `event,microseconds`。
- `cold_start`：进程创建到首个可读视口完成绘制。
- `hot_open`：主实例收到文件后，到首个可读视口完成绘制。
- `tab_switch`：切换操作开始到目标 Tab 完成绘制。
- `scroll`：滚轮事件进入处理到对应帧完成绘制。
- `paint`：一次文档绘制的耗时。

JSON 中的 `Pass` 只判定普通文档冷启动、热打开、Tab 切换和滚动门槛。10 MB 冷启动是挑战目标，不参与失败判定；10 MB 切换目前应单独查看 `Large10MB.TabSwitchMs`。

## 注意事项

- 首次运行可能被杀毒软件扫描，必须丢弃预热样本后再测。
- 不要只回传截图；JSON 和 CSV 才能复核分位数与长尾。
- 若切换了电源模式，请确保测试机仍是交流供电，并在结果目录中记录模式名称。
- 静态 EXE 仍依赖 Windows 自带的 UCRT、WinHTTP、GDI/GDI+ 等系统组件，不需要另外安装 MinGW DLL。
