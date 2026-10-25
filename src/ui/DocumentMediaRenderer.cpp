#include "ui/DocumentMediaRenderer.h"

#include "core/policy/ResourcePolicy.h"
#include "platform/ImageDownloader.h"
#include "ui/TextLayout.h"

#include <wx/event.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/window.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

wxString BlockMarker(const litemark::Block& block) {
    if (block.task) {
        return block.taskChecked ? U8("☑") : U8("☐");
    }
    if (block.listOrdered) {
        return wxString::Format("%d.", block.listIndex);
    }
    return U8("•");
}

wxFont ScaledFont(
    litemark::detail::TextStyle style,
    double fontScale) {
    style.pointSize =
        litemark::detail::ScaledPointSize(style.pointSize, fontScale);
    return litemark::detail::BuildFont(style);
}

}  // namespace

namespace litemark::detail {

struct DocumentMediaRenderer::Impl {
    struct Task {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    struct ResolvedImageSource {
        wxString key;
        wxString localPath;
        wxString url;
        bool remote = false;
        bool valid = false;
    };

    wxWindow* owner = nullptr;
    int imageLoadedEventId = 0;
    int flowchartLoadedEventId = 0;
    wxString basePath;
    std::map<wxString, std::shared_ptr<wxImage>> images;
    std::set<wxString> requestedImages;
    std::set<wxString> failedImages;
    std::vector<std::unique_ptr<Task>> imageTasks;
    std::map<std::string, FlowchartGraph> flowcharts;
    std::set<std::string> requestedFlowcharts;
    std::set<std::string> failedFlowcharts;
    std::vector<std::unique_ptr<Task>> flowchartTasks;

    ~Impl() {
        for (auto& task : imageTasks) {
            task->cancelled->store(true, std::memory_order_relaxed);
        }
        for (auto& task : imageTasks) {
            if (task->thread.joinable()) {
                task->thread.join();
            }
        }
        for (auto& task : flowchartTasks) {
            task->cancelled->store(true, std::memory_order_relaxed);
        }
        for (auto& task : flowchartTasks) {
            if (task->thread.joinable()) {
                task->thread.join();
            }
        }
    }

    int FromDIPValue(int value) const {
        const double scale =
            owner != nullptr ? owner->GetDPIScaleFactor() : 1.0;
        return static_cast<int>(std::lround(value * scale));
    }

    ResolvedImageSource ResolveImageSource(
        const std::string& source) const {
        ResolvedImageSource resolved;
        if (source.empty()) {
            return resolved;
        }
        const wxString text = wxString::FromUTF8(source);
        const ResourceReferenceKind referenceKind =
            ClassifyResourceReference(source);
        if (IsRemoteResource(referenceKind)) {
            resolved.key = text;
            resolved.url = text;
            resolved.remote = true;
            resolved.valid = true;
            return resolved;
        }
        if (referenceKind != ResourceReferenceKind::LocalOrRelative ||
            basePath.empty()) {
            return resolved;
        }

        wxFileName imagePath(text);
        if (imagePath.IsRelative()) {
            imagePath.MakeAbsolute(wxFileName(basePath).GetPath());
        }
        imagePath.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE |
                            wxPATH_NORM_LONG);
        if (!imagePath.FileExists()) {
            return resolved;
        }
        resolved.key = imagePath.GetFullPath();
        resolved.localPath = resolved.key;
        resolved.valid = true;
        return resolved;
    }

    void RequestImage(const ResolvedImageSource& source) {
        if (images.find(source.key) != images.end() ||
            failedImages.find(source.key) != failedImages.end() ||
            requestedImages.find(source.key) != requestedImages.end()) {
            return;
        }

        if (!source.remote) {
            wxFileName fileName(source.localPath);
            const wxString extension = fileName.GetExt().Lower();
            const wxScopedCharBuffer extensionUtf8 = extension.ToUTF8();
            if (!IsSupportedImageExtension(
                    std::string_view(extensionUtf8.data(),
                                     extensionUtf8.length()))) {
                failedImages.insert(source.key);
                return;
            }
            if (fileName.GetSize() > 64LL * 1024 * 1024) {
                failedImages.insert(source.key);
                return;
            }
        }

        requestedImages.insert(source.key);
        auto task = std::make_unique<Task>();
        task->cancelled = std::make_shared<std::atomic_bool>(false);
        const auto cancelled = task->cancelled;
        Task* rawTask = task.get();
        imageTasks.push_back(std::move(task));
        rawTask->thread = std::thread(
            [this, owner = owner, source, cancelled,
             eventId = imageLoadedEventId]() {
                auto image = std::make_shared<wxImage>();
                bool loaded = false;
                if (source.remote) {
                    std::vector<std::uint8_t> bytes;
                    wxString downloadError;
                    if (DownloadImageUrl(source.url, &bytes,
                                         &downloadError)) {
                        wxMemoryInputStream stream(
                            bytes.data(), bytes.size());
                        loaded = image->LoadFile(stream);
                    }
                } else {
                    loaded = image->LoadFile(source.localPath);
                }
                if (loaded) {
                    const long long pixels =
                        static_cast<long long>(image->GetWidth()) *
                        image->GetHeight();
                    loaded = image->IsOk() && pixels > 0 &&
                             pixels <= 64000000LL;
                }
                if (cancelled->load(std::memory_order_relaxed)) {
                    return;
                }
                auto* event = new wxThreadEvent(wxEVT_THREAD, eventId);
                event->SetPayload(ImageLoadedPayload{
                    source.key,
                    loaded ? std::move(image) : nullptr,
                });
                wxQueueEvent(owner, event);
            });
    }

    void RequestFlowchart(const std::string& source) {
        if (flowcharts.find(source) != flowcharts.end() ||
            failedFlowcharts.find(source) != failedFlowcharts.end() ||
            requestedFlowcharts.find(source) !=
                requestedFlowcharts.end()) {
            return;
        }

        requestedFlowcharts.insert(source);
        auto task = std::make_unique<Task>();
        task->cancelled = std::make_shared<std::atomic_bool>(false);
        const auto cancelled = task->cancelled;
        Task* rawTask = task.get();
        flowchartTasks.push_back(std::move(task));
        rawTask->thread = std::thread(
            [owner = owner, source, cancelled,
             eventId = flowchartLoadedEventId]() {
                FlowchartParseResult result =
                    ParseMermaidFlowchart(source);
                if (cancelled->load(std::memory_order_relaxed)) {
                    return;
                }
                auto* event = new wxThreadEvent(wxEVT_THREAD, eventId);
                event->SetPayload(FlowchartLoadedPayload{
                    source,
                    std::move(result),
                });
                wxQueueEvent(owner, event);
            });
    }

    wxPoint BoundaryPoint(
        const wxRect& rect,
        const wxPoint& toward) const {
        const double dx = toward.x - (rect.x + rect.width / 2.0);
        const double dy = toward.y - (rect.y + rect.height / 2.0);
        if (std::fabs(dx) < 0.01 && std::fabs(dy) < 0.01) {
            return wxPoint(
                rect.x + rect.width / 2, rect.y + rect.height / 2);
        }
        const double sx =
            std::fabs(dx) > 0.01
                ? std::fabs((rect.width / 2.0) / dx)
                : 1e9;
        const double sy =
            std::fabs(dy) > 0.01
                ? std::fabs((rect.height / 2.0) / dy)
                : 1e9;
        const double factor = std::min(sx, sy);
        return wxPoint(
            static_cast<int>(std::lround(
                rect.x + rect.width / 2.0 + dx * factor)),
            static_cast<int>(std::lround(
                rect.y + rect.height / 2.0 + dy * factor)));
    }

    void BuildFlowchartFallback(
        wxDC& dc,
        BlockLayout& item,
        int width,
        double scale,
        double fontScale,
        FontMap* fonts) {
        item.flowchartFallback = true;
        TextStyle style;
        style.pointSize = 10;
        style.mono = true;
        style.code = true;
        std::vector<InlineSpan> raw(1);
        raw[0].text = item.block->flowSource.empty()
                         ? item.block->raw
                         : item.block->flowSource;
        const int padding =
            static_cast<int>(std::lround(10 * scale));
        BuildTextLayout(
            dc, item.text, width - padding * 2, raw, style,
            fontScale, fonts);
        item.height = item.text.height + padding * 2;
    }

    void BuildFlowchartGraphLayout(
        wxDC& dc,
        BlockLayout& item,
        const FlowchartGraph& graph,
        int width,
        double scale,
        double fontScale,
        FontMap* fonts) {
        if (graph.nodes.empty()) {
            BuildFlowchartFallback(
                dc, item, width, scale, fontScale, fonts);
            return;
        }

        auto layout = std::make_unique<BlockLayout::FlowchartLayout>();
        const int margin = static_cast<int>(std::lround(16 * scale));
        const int labelMaxWidth =
            static_cast<int>(std::lround(190 * scale));
        const int nodePaddingX =
            static_cast<int>(std::lround(18 * scale));
        const int nodePaddingY =
            static_cast<int>(std::lround(10 * scale));
        const int minNodeWidth =
            static_cast<int>(std::lround(84 * scale));
        const int minNodeHeight =
            static_cast<int>(std::lround(38 * scale));
        const int nodeGap =
            static_cast<int>(std::lround(26 * scale));
        const int rankGap =
            static_cast<int>(std::lround(58 * scale));

        layout->nodes.reserve(graph.nodes.size());
        for (const auto& node : graph.nodes) {
            BlockLayout::FlowchartLayout::Node nodeLayout;
            nodeLayout.source = &node;
            TextStyle style;
            style.pointSize = 10;
            std::vector<InlineSpan> label(1);
            label[0].text = node.label;
            BuildTextLayout(
                dc, nodeLayout.label, labelMaxWidth, label, style,
                fontScale, fonts);
            int textWidth = 0;
            for (const auto& line : nodeLayout.label.lines) {
                textWidth = std::max(textWidth, line.width);
            }
            int nodeWidth =
                std::max(minNodeWidth, textWidth + nodePaddingX * 2);
            int nodeHeight = std::max(
                minNodeHeight,
                nodeLayout.label.height + nodePaddingY * 2);
            if (node.shape == FlowNodeShape::Diamond) {
                nodeWidth = static_cast<int>(
                    std::lround(nodeWidth * 1.25));
                nodeHeight = static_cast<int>(
                    std::lround(nodeHeight * 1.35));
            } else if (node.shape == FlowNodeShape::Circle) {
                const int diameter = std::max(nodeWidth, nodeHeight);
                nodeWidth = diameter;
                nodeHeight = diameter;
            }
            nodeLayout.rect = wxRect(0, 0, nodeWidth, nodeHeight);
            layout->nodes.push_back(std::move(nodeLayout));
        }

        std::map<std::string, int> index;
        for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
            index[graph.nodes[i].id] = static_cast<int>(i);
        }

        std::vector<std::vector<int>> outgoing(graph.nodes.size());
        std::vector<int> edgeTargets(graph.edges.size(), -1);
        for (std::size_t edge = 0; edge < graph.edges.size(); ++edge) {
            const auto from = index.find(graph.edges[edge].from);
            const auto to = index.find(graph.edges[edge].to);
            if (from == index.end() || to == index.end()) {
                BuildFlowchartFallback(
                    dc, item, width, scale, fontScale, fonts);
                return;
            }
            edgeTargets[edge] = to->second;
            outgoing[from->second].push_back(
                static_cast<int>(edge));
        }

        std::vector<bool> ignored(graph.edges.size(), false);
        std::vector<int> color(graph.nodes.size(), 0);
        std::function<void(int)> dfs = [&](int node) {
            color[node] = 1;
            for (int edgeIndex : outgoing[node]) {
                const int target = edgeTargets[edgeIndex];
                if (color[target] == 1) {
                    ignored[edgeIndex] = true;
                } else if (color[target] == 0) {
                    dfs(target);
                }
            }
            color[node] = 2;
        };
        for (std::size_t node = 0; node < graph.nodes.size(); ++node) {
            if (color[node] == 0) {
                dfs(static_cast<int>(node));
            }
        }

        std::vector<int> indegree(graph.nodes.size(), 0);
        for (std::size_t edge = 0; edge < graph.edges.size(); ++edge) {
            if (!ignored[edge]) {
                ++indegree[edgeTargets[edge]];
            }
        }
        std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
        for (std::size_t node = 0; node < graph.nodes.size(); ++node) {
            if (indegree[node] == 0) {
                ready.push(static_cast<int>(node));
            }
        }

        std::vector<int> layers(graph.nodes.size(), 0);
        while (!ready.empty()) {
            const int node = ready.top();
            ready.pop();
            for (int edgeIndex : outgoing[node]) {
                if (ignored[edgeIndex]) {
                    continue;
                }
                const int target = edgeTargets[edgeIndex];
                layers[target] =
                    std::max(layers[target], layers[node] + 1);
                if (--indegree[target] == 0) {
                    ready.push(target);
                }
            }
        }

        const int layerCount =
            *std::max_element(layers.begin(), layers.end()) + 1;
        std::vector<std::vector<int>> ranks(layerCount);
        for (std::size_t node = 0; node < layers.size(); ++node) {
            ranks[layers[node]].push_back(static_cast<int>(node));
        }

        const bool vertical =
            graph.direction == FlowDirection::TopDown ||
            graph.direction == FlowDirection::BottomUp;
        if (vertical) {
            std::vector<int> layerHeight(layerCount, minNodeHeight);
            std::vector<int> layerWidth(layerCount, 0);
            for (int layer = 0; layer < layerCount; ++layer) {
                for (std::size_t pos = 0;
                     pos < ranks[layer].size(); ++pos) {
                    const auto& rect =
                        layout->nodes[ranks[layer][pos]].rect;
                    layerHeight[layer] =
                        std::max(layerHeight[layer], rect.height);
                    layerWidth[layer] += rect.width;
                    if (pos + 1 < ranks[layer].size()) {
                        layerWidth[layer] += nodeGap;
                    }
                }
            }
            const int canvasWidth = *std::max_element(
                layerWidth.begin(), layerWidth.end());
            int y = margin;
            for (int layer = 0; layer < layerCount; ++layer) {
                int x =
                    margin + (canvasWidth - layerWidth[layer]) / 2;
                for (int nodeIndex : ranks[layer]) {
                    auto& rect = layout->nodes[nodeIndex].rect;
                    rect.x = x;
                    rect.y =
                        y + (layerHeight[layer] - rect.height) / 2;
                    x += rect.width + nodeGap;
                }
                y += layerHeight[layer] + rankGap;
            }
            layout->width = canvasWidth + margin * 2;
            layout->height = y - rankGap + margin;
        } else {
            std::vector<int> layerWidth(layerCount, minNodeWidth);
            std::vector<int> layerHeight(layerCount, 0);
            for (int layer = 0; layer < layerCount; ++layer) {
                for (std::size_t pos = 0;
                     pos < ranks[layer].size(); ++pos) {
                    const auto& rect =
                        layout->nodes[ranks[layer][pos]].rect;
                    layerWidth[layer] =
                        std::max(layerWidth[layer], rect.width);
                    layerHeight[layer] += rect.height;
                    if (pos + 1 < ranks[layer].size()) {
                        layerHeight[layer] += nodeGap;
                    }
                }
            }
            const int canvasHeight = *std::max_element(
                layerHeight.begin(), layerHeight.end());
            int x = margin;
            for (int layer = 0; layer < layerCount; ++layer) {
                int y =
                    margin + (canvasHeight - layerHeight[layer]) / 2;
                for (int nodeIndex : ranks[layer]) {
                    auto& rect = layout->nodes[nodeIndex].rect;
                    rect.x =
                        x + (layerWidth[layer] - rect.width) / 2;
                    rect.y = y;
                    y += rect.height + nodeGap;
                }
                x += layerWidth[layer] + rankGap;
            }
            layout->width = x - rankGap + margin;
            layout->height = canvasHeight + margin * 2;
        }

        if (graph.direction == FlowDirection::BottomUp) {
            for (auto& node : layout->nodes) {
                node.rect.y =
                    layout->height - node.rect.y - node.rect.height;
            }
        } else if (graph.direction == FlowDirection::RightLeft) {
            for (auto& node : layout->nodes) {
                node.rect.x =
                    layout->width - node.rect.x - node.rect.width;
            }
        }

        for (const auto& cluster : graph.clusters) {
            wxRect bounds;
            bool first = true;
            for (const auto& nodeId : cluster.nodeIds) {
                const auto found = index.find(nodeId);
                if (found == index.end()) {
                    continue;
                }
                const wxRect& rect =
                    layout->nodes[found->second].rect;
                bounds = first ? rect : bounds.Union(rect);
                first = false;
            }
            if (!first) {
                const int padding =
                    static_cast<int>(std::lround(14 * scale));
                bounds.x -= padding;
                bounds.y -= padding +
                            static_cast<int>(std::lround(20 * scale));
                bounds.width += padding * 2;
                bounds.height += padding * 2 +
                                 static_cast<int>(
                                     std::lround(20 * scale));
                BlockLayout::FlowchartLayout::Cluster clusterLayout;
                clusterLayout.source = &cluster;
                clusterLayout.rect = bounds;
                clusterLayout.title =
                    wxString::FromUTF8(cluster.title);
                layout->clusters.push_back(
                    std::move(clusterLayout));
                layout->width =
                    std::max(layout->width, bounds.GetRight() + margin);
                layout->height = std::max(
                    layout->height, bounds.GetBottom() + margin);
            }
        }

        for (const auto& edge : graph.edges) {
            const auto from = index.find(edge.from);
            const auto to = index.find(edge.to);
            if (from == index.end() || to == index.end()) {
                continue;
            }
            const wxRect& fromRect =
                layout->nodes[from->second].rect;
            const wxRect& toRect =
                layout->nodes[to->second].rect;
            const wxPoint fromCenter(
                fromRect.x + fromRect.width / 2,
                fromRect.y + fromRect.height / 2);
            const wxPoint toCenter(
                toRect.x + toRect.width / 2,
                toRect.y + toRect.height / 2);
            BlockLayout::FlowchartLayout::Edge edgeLayout;
            edgeLayout.source = &edge;
            edgeLayout.from = BoundaryPoint(fromRect, toCenter);
            edgeLayout.to = BoundaryPoint(toRect, fromCenter);
            edgeLayout.arrow =
                edge.style != FlowEdgeStyle::NoArrow;
            if (!edge.label.empty()) {
                TextStyle labelStyle;
                labelStyle.pointSize = 9;
                std::vector<InlineSpan> label(1);
                label[0].text = edge.label;
                BuildTextLayout(
                    dc, edgeLayout.label, FromDIPValue(180), label,
                    labelStyle, fontScale, fonts);
                const int labelX =
                    (edgeLayout.from.x + edgeLayout.to.x) / 2 -
                    edgeLayout.label.lines.front().width / 2;
                const int labelY =
                    (edgeLayout.from.y + edgeLayout.to.y) / 2 -
                    edgeLayout.label.height / 2;
                edgeLayout.labelRect = wxRect(
                    labelX - FromDIPValue(4),
                    labelY - FromDIPValue(2),
                    edgeLayout.label.lines.front().width +
                        FromDIPValue(8),
                    edgeLayout.label.height + FromDIPValue(4));
            }
            layout->edges.push_back(std::move(edgeLayout));
        }

        item.flowchart = std::move(layout);
        item.width =
            std::min(width, item.flowchart->width);
        item.height = item.flowchart->height;
    }
};

DocumentMediaRenderer::DocumentMediaRenderer(
    wxWindow* owner,
    int imageLoadedEventId,
    int flowchartLoadedEventId)
    : impl_(std::make_unique<Impl>()) {
    impl_->owner = owner;
    impl_->imageLoadedEventId = imageLoadedEventId;
    impl_->flowchartLoadedEventId = flowchartLoadedEventId;
}

DocumentMediaRenderer::~DocumentMediaRenderer() = default;

void DocumentMediaRenderer::SetBasePath(
    const wxString& basePath) {
    impl_->basePath = basePath;
}

void DocumentMediaRenderer::BuildImageLayout(
    wxDC& dc,
    BlockLayout& item,
    int width,
    double scale) {
    (void)dc;
    const Block& block = *item.block;
    item.marker = block.listMarker ? BlockMarker(block) : wxString();
    if (block.listMarker) {
        item.markerY = item.y;
    }

    const Impl::ResolvedImageSource source =
        impl_->ResolveImageSource(block.imagePath);
    if (!source.valid) {
        item.placeholderText =
            U8("图片不可用\n") +
            (block.imageAlt.empty()
                 ? wxString::FromUTF8(block.imagePath)
                 : wxString::FromUTF8(block.imageAlt));
        item.height = static_cast<int>(std::lround(120 * scale));
        return;
    }

    const auto found = impl_->images.find(source.key);
    if (found == impl_->images.end() ||
        !found->second || !found->second->IsOk()) {
        impl_->RequestImage(source);
        item.placeholderText =
            impl_->failedImages.find(source.key) !=
                    impl_->failedImages.end()
                ? U8("图片加载失败\n") +
                      (block.imageAlt.empty()
                           ? wxString::FromUTF8(block.imagePath)
                           : wxString::FromUTF8(block.imageAlt))
                : U8("正在加载图片…");
        item.height = static_cast<int>(std::lround(120 * scale));
        return;
    }

    const wxImage& image = *found->second;
    int targetWidth = std::min(width, image.GetWidth());
    int targetHeight = std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(image.GetHeight()) *
            targetWidth / image.GetWidth())));
    const int maxHeight =
        static_cast<int>(std::lround(1600 * scale));
    if (targetHeight > maxHeight) {
        targetHeight = maxHeight;
        targetWidth = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(image.GetWidth()) *
                targetHeight / image.GetHeight())));
    }

    wxImage scaled = image;
    if (scaled.GetWidth() != targetWidth ||
        scaled.GetHeight() != targetHeight) {
        scaled = scaled.Scale(
            targetWidth, targetHeight, wxIMAGE_QUALITY_HIGH);
    }
    item.imageBitmap = wxBitmap(scaled);
    item.width = targetWidth;
    item.height = targetHeight;
}

void DocumentMediaRenderer::BuildFlowchartLayout(
    wxDC& dc,
    BlockLayout& item,
    int width,
    double scale,
    double fontScale,
    FontMap* fonts) {
    const Block& block = *item.block;
    if (block.flowSource.empty()) {
        impl_->BuildFlowchartFallback(
            dc, item, width, scale, fontScale, fonts);
        return;
    }
    if (impl_->failedFlowcharts.find(block.flowSource) !=
        impl_->failedFlowcharts.end()) {
        impl_->BuildFlowchartFallback(
            dc, item, width, scale, fontScale, fonts);
        return;
    }
    const auto found =
        impl_->flowcharts.find(block.flowSource);
    if (found == impl_->flowcharts.end()) {
        impl_->RequestFlowchart(block.flowSource);
        item.placeholderText =
            U8("正在解析 Mermaid flowchart…");
        item.height = static_cast<int>(std::lround(150 * scale));
        return;
    }
    impl_->BuildFlowchartGraphLayout(
        dc, item, found->second, width, scale, fontScale, fonts);
}

void DocumentMediaRenderer::DrawImage(
    wxDC& dc,
    const BlockLayout& item,
    const ThemePalette& palette,
    double fontScale) {
    if (item.imageBitmap.IsOk()) {
        dc.DrawBitmap(item.imageBitmap, item.x, item.y, true);
    } else {
        dc.SetPen(wxPen(palette.border, 1));
        dc.SetBrush(wxBrush(palette.codeBackground));
        dc.DrawRoundedRectangle(
            item.x, item.y, item.width, item.height,
            impl_->FromDIPValue(6));
        TextStyle style;
        style.pointSize = 10;
        dc.SetFont(ScaledFont(style, fontScale));
        dc.SetTextForeground(palette.mutedText);
        const wxArrayString lines =
            wxSplit(item.placeholderText, '\n', '\0');
        int totalHeight = 0;
        std::vector<wxSize> sizes;
        sizes.reserve(lines.size());
        for (const auto& line : lines) {
            const wxSize size = dc.GetTextExtent(line);
            sizes.push_back(size);
            totalHeight += size.y;
        }
        int textY = item.y + (item.height - totalHeight) / 2;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            dc.DrawText(
                lines[i],
                item.x + (item.width - sizes[i].x) / 2,
                textY);
            textY += sizes[i].y;
        }
    }

    if (!item.marker.empty()) {
        TextStyle markerStyle;
        markerStyle.pointSize = 11;
        markerStyle.bold = item.block->task;
        dc.SetFont(ScaledFont(markerStyle, fontScale));
        dc.SetTextForeground(palette.mutedText);
        dc.DrawText(item.marker, item.markerX, item.markerY);
    }
}

void DocumentMediaRenderer::DrawFlowchart(
    wxDC& dc,
    const BlockLayout& item,
    const ThemePalette& palette,
    double fontScale,
    const std::function<void(
        wxDC&, const TextLayout&, int, int)>& drawText) {
    if (item.flowchartFallback) {
        dc.SetPen(wxPen(palette.border, 1));
        dc.SetBrush(wxBrush(palette.codeBackground));
        dc.DrawRectangle(item.x, item.y, item.width, item.height);
        const int padding = impl_->FromDIPValue(10);
        drawText(
            dc, item.text, item.x + padding, item.y + padding);
        return;
    }
    if (!item.flowchart) {
        dc.SetPen(wxPen(palette.border, 1));
        dc.SetBrush(wxBrush(palette.codeBackground));
        dc.DrawRoundedRectangle(
            item.x, item.y, item.width, item.height,
            impl_->FromDIPValue(6));
        TextStyle style;
        style.pointSize = 10;
        dc.SetFont(ScaledFont(style, fontScale));
        dc.SetTextForeground(palette.mutedText);
        dc.DrawLabel(
            item.placeholderText,
            wxRect(
                item.x + impl_->FromDIPValue(10),
                item.y + impl_->FromDIPValue(10),
                item.width - impl_->FromDIPValue(20),
                item.height - impl_->FromDIPValue(20)),
            wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
        return;
    }

    dc.SetClippingRegion(item.x, item.y, item.width, item.height);
    for (const auto& cluster : item.flowchart->clusters) {
        wxRect rect = cluster.rect;
        rect.x += item.x;
        rect.y += item.y;
        dc.SetPen(wxPen(
            palette.border, 1, wxPENSTYLE_SHORT_DASH));
        dc.SetBrush(wxBrush(palette.windowBackground));
        dc.DrawRoundedRectangle(
            rect, impl_->FromDIPValue(8));
        dc.SetFont(ScaledFont(TextStyle{10, true}, fontScale));
        dc.SetTextForeground(palette.mutedText);
        dc.DrawText(
            cluster.title,
            rect.x + impl_->FromDIPValue(10),
            rect.y + impl_->FromDIPValue(7));
    }

    for (const auto& edge : item.flowchart->edges) {
        const wxPoint from(
            item.x + edge.from.x, item.y + edge.from.y);
        const wxPoint to(
            item.x + edge.to.x, item.y + edge.to.y);
        int width = 1;
        wxPenStyle penStyle = wxPENSTYLE_SOLID;
        if (edge.source->style == FlowEdgeStyle::Dotted) {
            penStyle = wxPENSTYLE_SHORT_DASH;
        } else if (edge.source->style == FlowEdgeStyle::Thick) {
            width = 2;
        }
        dc.SetPen(wxPen(palette.mutedText, width, penStyle));
        dc.DrawLine(from, to);

        if (edge.arrow) {
            const double dx = to.x - from.x;
            const double dy = to.y - from.y;
            const double length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.01) {
                const double ux = dx / length;
                const double uy = dy / length;
                const double arrowLength =
                    impl_->FromDIPValue(10);
                const double arrowHalfWidth =
                    impl_->FromDIPValue(4);
                const wxPoint base(
                    static_cast<int>(std::lround(
                        to.x - ux * arrowLength)),
                    static_cast<int>(std::lround(
                        to.y - uy * arrowLength)));
                const wxPoint points[] = {
                    to,
                    wxPoint(
                        static_cast<int>(std::lround(
                            base.x - uy * arrowHalfWidth)),
                        static_cast<int>(std::lround(
                            base.y + ux * arrowHalfWidth))),
                    wxPoint(
                        static_cast<int>(std::lround(
                            base.x + uy * arrowHalfWidth)),
                        static_cast<int>(std::lround(
                            base.y - ux * arrowHalfWidth))),
                };
                dc.SetBrush(wxBrush(palette.mutedText));
                dc.DrawPolygon(3, points);
            }
        }

        if (!edge.label.lines.empty()) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(palette.documentBackground));
            wxRect labelRect = edge.labelRect;
            labelRect.x += item.x;
            labelRect.y += item.y;
            dc.DrawRectangle(labelRect);
            drawText(
                dc, edge.label,
                labelRect.x + impl_->FromDIPValue(4),
                labelRect.y + impl_->FromDIPValue(2));
        }
    }

    for (const auto& node : item.flowchart->nodes) {
        wxRect rect = node.rect;
        rect.x += item.x;
        rect.y += item.y;
        dc.SetPen(wxPen(palette.border, 1));
        dc.SetBrush(wxBrush(palette.codeBackground));
        switch (node.source->shape) {
            case FlowNodeShape::Diamond: {
                const wxPoint points[] = {
                    wxPoint(rect.x + rect.width / 2, rect.y),
                    wxPoint(
                        rect.x + rect.width,
                        rect.y + rect.height / 2),
                    wxPoint(
                        rect.x + rect.width / 2,
                        rect.y + rect.height),
                    wxPoint(
                        rect.x, rect.y + rect.height / 2),
                };
                dc.DrawPolygon(4, points);
                break;
            }
            case FlowNodeShape::Circle:
                dc.DrawEllipse(rect);
                break;
            case FlowNodeShape::Rounded:
                dc.DrawRoundedRectangle(
                    rect, impl_->FromDIPValue(12));
                break;
            default:
                dc.DrawRectangle(rect);
                break;
        }
        int labelWidth = 0;
        for (const auto& line : node.label.lines) {
            labelWidth = std::max(labelWidth, line.width);
        }
        drawText(
            dc, node.label,
            rect.x + (rect.width - labelWidth) / 2,
            rect.y + (rect.height - node.label.height) / 2);
    }
    dc.DestroyClippingRegion();
}

void DocumentMediaRenderer::HandleImageLoaded(
    const ImageLoadedPayload& payload) {
    impl_->requestedImages.erase(payload.key);
    if (payload.image && payload.image->IsOk()) {
        impl_->images[payload.key] = payload.image;
        impl_->failedImages.erase(payload.key);
    } else {
        impl_->failedImages.insert(payload.key);
    }
}

void DocumentMediaRenderer::HandleFlowchartLoaded(
    const FlowchartLoadedPayload& payload) {
    impl_->requestedFlowcharts.erase(payload.source);
    if (payload.result.ok) {
        impl_->flowcharts[payload.source] =
            payload.result.graph;
        impl_->failedFlowcharts.erase(payload.source);
    } else {
        impl_->failedFlowcharts.insert(payload.source);
    }
}

std::size_t DocumentMediaRenderer::LoadedImageCount() const {
    return impl_->images.size();
}

std::size_t DocumentMediaRenderer::LoadedFlowchartCount() const {
    return impl_->flowcharts.size();
}

}  // namespace litemark::detail
