# DocumentView 拆分记录

## 1. 拆分目标

`DocumentView.cpp` 原先约 2825 行，同时包含：

- 布局数据结构。
- 字体选择和文本换行。
- 表格列宽和行布局。
- 图片与 flowchart 异步状态。
- 选择、命中测试、滚动和绘制。

本轮优先拆出无交互状态依赖的布局代码，避免为了“文件更小”引入高风险的大规模状态迁移。

## 2. 当前模块

| 文件 | 职责 |
|---|---|
| `ui/BlockLayout.h` | TextStyle、Line、BlockLayout、ViewLayout、FlowchartLayout、缓存结构 |
| `ui/TextLayout.h/.cpp` | 字体、文本换行、段落间距、UTF-8 段内偏移计算 |
| `ui/TableLayout.h/.cpp` | 内容感知列宽、表格行构造、水平滚动范围 |
| `ui/DocumentView.cpp` | 滚动、选择、命中测试、图片/flowchart 状态、事件和绘制编排 |

拆分后 `DocumentView.cpp` 约 2300 行，减少约 525 行。

## 3. 保持不变

- `DocumentView` 的公开接口不变。
- 布局结果、绘制顺序、滚动范围和交互行为不变。
- 图片和 flowchart 的线程生命周期仍由 `Impl` 管理。
- 表格分块、长文本分片和列表缩进规则不变。

## 4. 验证

- Release static 构建通过。
- CTest 14/14 通过。
- 滚动位置、跨块复制、搜索、锚点、三态目录、最近文档和工作区恢复全部通过。
- 5000 行表格和 9.5 MB 单段落压力脚本回归通过。

## 5. 后续候选

如果继续拆分，建议按以下边界推进，而不是继续移动零散函数：

1. `DocumentPaint.cpp`：集中 DrawTextLayout、表格、图片和 flowchart 绘制。
2. `DocumentInteraction.cpp`：选择、链接命中、表格横向拖动、鼠标事件。
3. `DocumentMedia.cpp`：图片下载/解码、flowchart 异步任务和缓存生命周期。

这些模块都依赖 `DocumentView::Impl`，下一阶段应先划出稳定的上下文接口。
