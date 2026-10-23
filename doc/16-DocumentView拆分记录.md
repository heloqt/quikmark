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
| `ui/DocumentMediaRenderer.h/.cpp` | 图片下载/解码缓存、flowchart 异步解析、媒体布局和绘制 |
| `ui/DocumentView.cpp` | 滚动、选择、命中测试、文本/表格绘制、事件编排 |

拆分后 `DocumentView.cpp` 约 1580 行，减少约 1240 行；媒体模块约 890 行。

## 3. 保持不变

- `DocumentView` 的公开接口不变。
- 布局结果、绘制顺序、滚动范围和交互行为不变。
- 图片和 flowchart 的线程生命周期由 `DocumentMediaRenderer` 统一管理。
- 表格分块、长文本分片和列表缩进规则不变。

## 4. 验证

- Release static 构建通过。
- CTest 14/14 通过。
- 滚动位置、跨块复制、搜索、锚点、三态目录、最近文档和工作区恢复全部通过。
- 5000 行表格和 9.5 MB 单段落压力脚本回归通过。
- 本地 PNG 和 Mermaid flowchart 异步加载缓存测试通过。

## 5. 后续候选

如果继续拆分，建议按以下边界推进，而不是继续移动零散函数：

1. `DocumentPaint.cpp`：集中 DrawTextLayout 和表格绘制。
2. `DocumentInteraction.cpp`：选择、链接命中、表格横向拖动、鼠标事件。

剩余绘制和交互模块都依赖 `DocumentView::Impl`，下一阶段应先划出稳定的上下文接口。
