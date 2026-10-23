#pragma once

#include "core/flow/Flowchart.h"
#include "core/model/Block.h"

#include <wx/bitmap.h>
#include <wx/font.h>
#include <wx/geometry.h>
#include <wx/string.h>

#include <cstddef>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace litemark::detail {

struct TextStyle {
    int pointSize = 11;
    bool bold = false;
    bool italic = false;
    bool mono = false;
    bool underline = false;
    bool strikethrough = false;
    bool code = false;
    bool image = false;
    bool link = false;

    auto key() const {
        return std::tie(pointSize, bold, italic, mono, underline);
    }
};

struct Segment {
    wxString text;
    TextStyle style;
    wxString href;
    int x = 0;
    int width = 0;
    std::size_t sourceStart = 0;
    std::size_t sourceEnd = 0;
};

struct Line {
    std::vector<Segment> segments;
    int y = 0;
    int height = 0;
    int width = 0;
};

struct TextLayout {
    std::vector<Line> lines;
    int height = 0;
};

struct CellLayout {
    TextLayout text;
    int x = 0;
    int width = 0;
};

struct RowLayout {
    std::vector<CellLayout> cells;
    int y = 0;
    int height = 0;
};

struct BlockLayout {
    const Block* block = nullptr;
    std::size_t blockIndex = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    TextLayout text;
    std::vector<RowLayout> tableRows;
    int tableContentWidth = 0;
    int tableRowsHeight = 0;
    int tableScrollX = 0;
    int tableScrollbarHeight = 0;
    wxBitmap imageBitmap;
    wxString placeholderText;
    bool flowchartFallback = false;
    struct FlowchartLayout;
    std::unique_ptr<FlowchartLayout> flowchart;
    wxString marker;
    int markerX = 0;
    int markerGutter = 0;
    int markerY = 0;
};

struct BlockLayout::FlowchartLayout {
    struct Node {
        const FlowNode* source = nullptr;
        wxRect rect;
        TextLayout label;
    };

    struct Edge {
        const FlowEdge* source = nullptr;
        wxPoint from;
        wxPoint to;
        bool arrow = true;
        TextLayout label;
        wxRect labelRect;
    };

    struct Cluster {
        const FlowCluster* source = nullptr;
        wxRect rect;
        wxString title;
    };

    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Cluster> clusters;
    int width = 0;
    int height = 0;
};

struct BlockSlot {
    int estimatedHeight = 0;
    int actualHeight = -1;
};

struct ViewLayout {
    std::vector<BlockSlot> slots;
    std::map<std::size_t, BlockLayout> blockLayouts;
    int estimatedTotalHeight = 0;
    int width = 0;
};

struct LayoutCacheEntry {
    std::shared_ptr<const MarkdownDocument> document;
    std::shared_ptr<ViewLayout> layout;
};

using FontMap =
    std::map<std::tuple<int, bool, bool, bool, bool>, wxFont>;

}  // namespace litemark::detail
