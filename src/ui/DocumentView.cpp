#include "ui/DocumentView.h"

#include <wx/dc.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/font.h>
#include <wx/fontenum.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr double kMouseWheelPageFraction = 0.45;

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

std::size_t CountUtf8Characters(const std::string& text) {
    std::size_t count = 0;
    for (unsigned char value : text) {
        if ((value & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

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
    int x = 0;
    int width = 0;
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
    const litemark::Block* block = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    TextLayout text;
    std::vector<RowLayout> tableRows;
    wxString marker;
    int markerX = 0;
    int markerY = 0;
};

struct BlockSlot {
    int estimatedHeight = 0;
    int actualHeight = -1;
};

struct ViewLayout {
    std::vector<BlockSlot> slots;
    std::map<std::size_t, BlockLayout> blockLayouts;
    int width = 0;
};

struct LayoutCacheEntry {
    std::shared_ptr<const litemark::MarkdownDocument> document;
    std::shared_ptr<ViewLayout> layout;
};

int LineHeight(wxDC& dc) {
    return std::max(14, dc.GetCharHeight() + 2);
}

wxString ChooseFace(bool mono) {
    if (mono) {
        if (wxFontEnumerator::IsValidFacename("Cascadia Mono")) {
            return "Cascadia Mono";
        }
        return "Consolas";
    }
    if (wxFontEnumerator::IsValidFacename("Microsoft YaHei UI")) {
        return "Microsoft YaHei UI";
    }
    return "Segoe UI";
}

wxFont BuildFont(const TextStyle& style) {
    wxFont font(wxFontInfo(style.pointSize).FaceName(ChooseFace(style.mono)));
    if (style.bold) {
        font.SetWeight(wxFONTWEIGHT_BOLD);
    }
    if (style.italic) {
        font.SetStyle(wxFONTSTYLE_ITALIC);
    }
    if (style.underline) {
        font.SetUnderlined(true);
    }
    return font;
}

TextStyle BaseStyle(const litemark::Block& block) {
    TextStyle style;
    switch (block.kind) {
        case litemark::BlockKind::Heading:
            style.bold = true;
            style.pointSize = std::max(12, 25 - block.headingLevel * 2);
            break;
        case litemark::BlockKind::Code:
        case litemark::BlockKind::Html:
            style.pointSize = 10;
            style.mono = true;
            style.code = true;
            break;
        default:
            style.pointSize = 11;
            break;
    }
    return style;
}

int BreakPosition(const wxString& text, int fit) {
    if (fit <= 0) {
        return 0;
    }
    if (fit >= static_cast<int>(text.length())) {
        return static_cast<int>(text.length());
    }

    const int lowerBound = std::max(1, fit / 3);
    for (int i = fit - 1; i >= lowerBound; --i) {
        if (text[i] == ' ' || text[i] == '\t') {
            return i;
        }
    }
    return fit;
}

using FontMap =
    std::map<std::tuple<int, bool, bool, bool, bool>, wxFont>;

void BuildTextLayout(wxDC& dc, TextLayout& layout, int maxWidth,
                     const std::vector<litemark::InlineSpan>& spans,
                     const TextStyle& baseStyle, FontMap* fonts) {
    maxWidth = std::max(20, maxWidth);
    auto fontFor = [&](const TextStyle& style) -> wxFont {
        if (fonts == nullptr) {
            return BuildFont(style);
        }
        auto result = fonts->try_emplace(style.key(), BuildFont(style));
        return result.first->second;
    };

    auto ensureLine = [&]() -> Line& {
        if (layout.lines.empty()) {
            layout.lines.emplace_back();
        }
        return layout.lines.back();
    };

    auto newLine = [&]() {
        if (layout.lines.empty()) {
            layout.lines.emplace_back();
            return;
        }
        Line& previous = layout.lines.back();
        if (previous.segments.empty() && previous.width == 0) {
            return;
        }
        Line line;
        line.y = previous.y + previous.height + 3;
        layout.lines.push_back(std::move(line));
    };

    auto refreshHeight = [&]() {
        if (!layout.lines.empty()) {
            const Line& last = layout.lines.back();
            layout.height = last.y + last.height;
        }
    };

    auto appendSegment = [&](const wxString& text, const TextStyle& style,
                             int width) {
        Line& line = ensureLine();
        dc.SetFont(fontFor(style));
        const int oldHeight = line.height;
        line.height = std::max(line.height, LineHeight(dc));
        if (!line.segments.empty()) {
            Segment& last = line.segments.back();
            const bool sameStyle =
                last.style.key() == style.key() &&
                last.style.code == style.code &&
                last.style.image == style.image &&
                last.style.link == style.link &&
                last.style.strikethrough == style.strikethrough;
            if (sameStyle) {
                last.text += text;
                last.width += width;
                line.width += width;
            } else {
                line.segments.push_back({text, style, line.width, width});
                line.width += width;
            }
        } else {
            line.segments.push_back({text, style, 0, width});
            line.width = width;
        }
        if (line.height < oldHeight) {
            line.height = oldHeight;
        }
        refreshHeight();
    };

    for (const auto& sourceSpan : spans) {
        wxString text = wxString::FromUTF8(sourceSpan.text);
        if (sourceSpan.image) {
            text = text.empty() ? U8("[图片]")
                                : U8("[图片: ") + text + U8("]");
        }
        if (text.empty()) {
            continue;
        }

        TextStyle style = baseStyle;
        style.bold = style.bold || sourceSpan.bold;
        style.italic = style.italic || sourceSpan.italic;
        style.strikethrough =
            style.strikethrough || sourceSpan.strikethrough;
        style.code = style.code || sourceSpan.code;
        style.mono = style.mono || sourceSpan.code;
        style.image = sourceSpan.image;
        style.link = !sourceSpan.image && !sourceSpan.href.empty();
        style.underline = style.underline || style.link;

        while (!text.empty()) {
            const int newline = text.Find('\n');
            wxString part = newline == wxNOT_FOUND ? text : text.Left(newline);
            const bool hardBreak = newline != wxNOT_FOUND;
            text = newline == wxNOT_FOUND ? wxString() : text.Mid(newline + 1);

            while (!part.empty()) {
                if (ensureLine().segments.empty() &&
                    (part.StartsWith(" ") || part.StartsWith("\t"))) {
                    part = part.Mid(1);
                    continue;
                }

                dc.SetFont(fontFor(style));
                Line& current = ensureLine();
                current.height = std::max(current.height, LineHeight(dc));
                refreshHeight();

                const int available = maxWidth - current.width;
                if (available <= 0) {
                    newLine();
                    continue;
                }

                wxArrayInt partial;
                dc.GetPartialTextExtents(part, partial);
                int fit = 0;
                for (std::size_t i = 0; i < partial.size(); ++i) {
                    if (partial[i] <= available) {
                        fit = static_cast<int>(i) + 1;
                    } else {
                        break;
                    }
                }
                if (fit == 0) {
                    if (!current.segments.empty()) {
                        newLine();
                        continue;
                    }
                    fit = 1;
                }

                fit = BreakPosition(part, fit);
                if (fit <= 0) {
                    fit = 1;
                }

                wxString piece = part.Left(fit);
                const int pieceWidth = dc.GetTextExtent(piece).x;
                appendSegment(piece, style, pieceWidth);
                part = part.Mid(fit);
                if (!part.empty()) {
                    newLine();
                }
            }

            if (hardBreak) {
                newLine();
            }
        }
    }

    if (layout.lines.empty()) {
        layout.lines.emplace_back();
        dc.SetFont(fontFor(baseStyle));
        layout.lines.back().height = LineHeight(dc);
    }
    refreshHeight();
}

int IntrinsicBefore(const litemark::Block& block) {
    if (block.kind == litemark::BlockKind::Heading) {
        return block.headingLevel <= 2 ? 16 : 12;
    }
    if (block.kind == litemark::BlockKind::Code ||
        block.kind == litemark::BlockKind::Table ||
        block.kind == litemark::BlockKind::Rule) {
        return 14;
    }
    return 8;
}

int IntrinsicAfter(const litemark::Block& block) {
    if (block.kind == litemark::BlockKind::Heading) {
        return 10;
    }
    if (block.kind == litemark::BlockKind::Code ||
        block.kind == litemark::BlockKind::Table) {
        return 14;
    }
    return 7;
}

}  // namespace

namespace litemark {

struct DocumentView::Impl {
    std::shared_ptr<const MarkdownDocument> document;
    ThemePalette palette;
    std::shared_ptr<ViewLayout> layout;
    bool layoutDirty = true;
    int layoutWidth = 0;
    int pendingScrollY = 0;
    FontMap fonts;
    std::map<const MarkdownDocument*, LayoutCacheEntry> layoutCache;
    wxString messageTitle = U8("liteMark");
    wxString messageBody = U8(
        "这是一个只读 Markdown 阅读器。\n"
        "使用“文件 > 打开”或把 Markdown 文件拖入窗口。");

    wxFont FontFor(const TextStyle& style) {
        auto result = fonts.try_emplace(style.key(), BuildFont(style));
        return result.first->second;
    }

    double Scale(wxDC& dc) const {
        return static_cast<double>(dc.GetPPI().x) / 96.0;
    }

    int PageMargin(wxDC& dc) const {
        return static_cast<int>(std::lround(34 * Scale(dc)));
    }

    int EstimateHeight(wxDC& dc, const Block& block, int width) const {
        const double scale = Scale(dc);
        const int before =
            static_cast<int>(std::lround(IntrinsicBefore(block) * scale));
        const int after =
            static_cast<int>(std::lround(IntrinsicAfter(block) * scale));

        if (block.kind == BlockKind::Rule) {
            return before + static_cast<int>(std::lround(18 * scale)) + after;
        }
        if (block.kind == BlockKind::Table) {
            const int rows = std::max(1, static_cast<int>(block.tableRows.size()));
            return before + rows * static_cast<int>(std::lround(30 * scale)) +
                   after;
        }

        std::size_t characters = 0;
        std::size_t newlines = 0;
        if (block.kind == BlockKind::Code || block.kind == BlockKind::Html) {
            characters = CountUtf8Characters(block.raw);
            newlines = static_cast<std::size_t>(
                std::count(block.raw.begin(), block.raw.end(), '\n'));
        } else {
            for (const auto& span : block.spans) {
                characters += CountUtf8Characters(span.text);
                newlines += static_cast<std::size_t>(
                    std::count(span.text.begin(), span.text.end(), '\n'));
            }
        }

        const int averageAdvance =
            std::max(6, static_cast<int>(std::lround(9 * scale)));
        const int charsPerLine = std::max(8, width / averageAdvance);
        const int wrappedLines =
            std::max<std::size_t>(1, (characters + charsPerLine - 1) / charsPerLine);
        const int lines = static_cast<int>(wrappedLines + newlines);

        int lineHeight = 22;
        if (block.kind == BlockKind::Heading) {
            lineHeight = std::max(24, 30 - block.headingLevel * 2);
        } else if (block.kind == BlockKind::Code ||
                   block.kind == BlockKind::Html) {
            lineHeight = 19;
        }
        const int body =
            static_cast<int>(std::lround((lines * lineHeight + 12) * scale));
        return before + body + after;
    }

    void InitializeLayout(wxDC& dc, int clientWidth) {
        layout = std::make_shared<ViewLayout>();
        layout->width = clientWidth;
        layoutWidth = clientWidth;
        layoutDirty = false;
        if (!document) {
            return;
        }

        const int pageMargin = PageMargin(dc);
        const int maxContentWidth =
            std::max(120, clientWidth - pageMargin * 2);
        layout->slots.reserve(document->blocks.size());
        for (const Block& block : document->blocks) {
            const int quoteIndent = static_cast<int>(
                std::lround(block.quoteDepth * 18 * Scale(dc)));
            const int listIndent =
                block.listDepth > 0
                    ? static_cast<int>(std::lround(
                          (block.listDepth - 1) * 22 * Scale(dc)))
                    : 0;
            const int width =
                std::max(80, maxContentWidth - quoteIndent - listIndent);
            layout->slots.push_back(
                BlockSlot{EstimateHeight(dc, block, width), -1});
        }

        if (document) {
            layoutCache[document.get()] =
                LayoutCacheEntry{document, layout};
        }
    }

    int SlotHeight(const BlockSlot& slot) const {
        return slot.actualHeight >= 0 ? slot.actualHeight
                                      : slot.estimatedHeight;
    }

    int TotalHeight() const {
        if (!layout) {
            return 0;
        }
        int total = 0;
        for (const auto& slot : layout->slots) {
            total += SlotHeight(slot);
        }
        return total;
    }

    BlockLayout BuildBlockLayout(wxDC& dc, std::size_t index, int y,
                                 int clientWidth) {
        const Block& block = document->blocks[index];
        const double scale = Scale(dc);
        const int pageMargin = static_cast<int>(std::lround(34 * scale));
        const int maxContentWidth =
            std::max(120, clientWidth - pageMargin * 2);
        const int quoteIndent =
            static_cast<int>(std::lround(block.quoteDepth * 18 * scale));
        const int listIndent =
            block.listDepth > 0
                ? static_cast<int>(
                      std::lround((block.listDepth - 1) * 22 * scale))
                : 0;
        const int x = pageMargin + quoteIndent + listIndent;
        const int width =
            std::max(80, maxContentWidth - quoteIndent - listIndent);
        const int before =
            static_cast<int>(std::lround(IntrinsicBefore(block) * scale));
        const int after =
            static_cast<int>(std::lround(IntrinsicAfter(block) * scale));

        BlockLayout item;
        item.block = &block;
        item.x = x;
        item.y = y + before;
        item.width = width;

        if (block.kind == BlockKind::Rule) {
            item.height = static_cast<int>(std::lround(18 * scale));
        } else if (block.kind == BlockKind::Table) {
            BuildTableLayout(dc, item, width, scale);
        } else if (block.kind == BlockKind::Code ||
                   block.kind == BlockKind::Html) {
            TextStyle style = BaseStyle(block);
            std::vector<InlineSpan> raw(1);
            raw[0].text = block.raw;
            const int padding = static_cast<int>(std::lround(10 * scale));
            BuildTextLayout(dc, item.text, width - padding * 2, raw, style,
                            &fonts);
            item.height = item.text.height + padding * 2;
        } else {
            TextStyle style = BaseStyle(block);
            const int markerWidth =
                block.listMarker
                    ? static_cast<int>(std::lround(27 * scale))
                    : 0;
            BuildTextLayout(dc, item.text, width - markerWidth, block.spans,
                            style, &fonts);
            item.height = item.text.height;
            if (block.listMarker) {
                item.marker = BlockMarker(block);
                item.markerX =
                    x - static_cast<int>(std::lround(26 * scale));
                item.markerY = item.y;
            }
        }

        const int actualHeight = before + item.height + after;
        BlockSlot& slot = layout->slots[index];
        slot.actualHeight = std::max(1, actualHeight);
        return item;
    }

    BlockLayout& LayoutFor(wxDC& dc, std::size_t index, int y,
                           int clientWidth) {
        auto found = layout->blockLayouts.find(index);
        const int before = static_cast<int>(
            std::lround(IntrinsicBefore(document->blocks[index]) * Scale(dc)));
        if (found == layout->blockLayouts.end()) {
            return layout->blockLayouts
                .emplace(index,
                         BuildBlockLayout(dc, index, y, clientWidth))
                .first->second;
        }
        found->second.y = y + before;
        if (found->second.block->listMarker) {
            found->second.markerY = found->second.y;
        }
        return found->second;
    }

    static wxString BlockMarker(const Block& block) {
        if (block.task) {
            return block.taskChecked ? U8("☑") : U8("☐");
        }
        if (block.listOrdered) {
            return wxString::Format("%d.", block.listIndex);
        }
        return U8("•");
    }

    void BuildTableLayout(wxDC& dc, BlockLayout& item, int width,
                          double scale) {
        const Block& block = *item.block;
        const std::size_t rowCount = block.tableRows.size();
        std::size_t columnCount = 0;
        for (const auto& row : block.tableRows) {
            columnCount = std::max(columnCount, row.size());
        }
        if (rowCount == 0 || columnCount == 0) {
            item.height = 20;
            return;
        }

        const int cellPadding =
            static_cast<int>(std::lround(7 * scale));
        const int columnWidth =
            std::max(50, (width - static_cast<int>(columnCount) - 1) /
                              static_cast<int>(columnCount));
        int y = 0;

        for (std::size_t r = 0; r < rowCount; ++r) {
            RowLayout rowLayout;
            rowLayout.y = y;
            for (std::size_t c = 0; c < columnCount; ++c) {
                CellLayout cell;
                cell.x = static_cast<int>(c) * (columnWidth + 1) + 1;
                cell.width = columnWidth;
                TextStyle style;
                style.pointSize = 10;
                style.bold = r == 0;
                if (c < block.tableRows[r].size()) {
                    BuildTextLayout(dc, cell.text,
                                    columnWidth - cellPadding * 2,
                                    block.tableRows[r][c].spans, style, &fonts);
                } else {
                    cell.text.height = 0;
                }
                rowLayout.cells.push_back(std::move(cell));
            }

            int rowHeight = static_cast<int>(std::lround(20 * scale));
            for (const auto& cell : rowLayout.cells) {
                rowHeight =
                    std::max(rowHeight, cell.text.height + cellPadding * 2);
            }
            rowLayout.height = rowHeight;
            y += rowHeight + 1;
            item.tableRows.push_back(std::move(rowLayout));
        }
        item.height = y + 1;
    }

    void DrawPlaceholder(wxDC& dc, int scrollX, int scrollY, int clientWidth) {
        TextStyle titleStyle;
        titleStyle.pointSize = 24;
        titleStyle.bold = true;
        TextStyle bodyStyle;
        bodyStyle.pointSize = 11;

        TextLayout title;
        std::vector<InlineSpan> titleSpan(1);
        titleSpan[0].text = messageTitle.ToUTF8().data();
        BuildTextLayout(dc, title, clientWidth - 80, titleSpan, titleStyle,
                        &fonts);

        TextLayout body;
        std::vector<InlineSpan> bodySpans(1);
        bodySpans[0].text = messageBody.ToUTF8().data();
        BuildTextLayout(dc, body, clientWidth - 80, bodySpans, bodyStyle,
                        &fonts);

        const int x = scrollX + 40;
        int y = scrollY + 36;
        DrawTextLayout(dc, title, x, y, false);
        y += title.height + 18;
        DrawTextLayout(dc, body, x, y, false);
    }

    void DrawTextLayout(wxDC& dc, const TextLayout& text, int baseX,
                        int baseY, bool blockCode) {
        for (const Line& line : text.lines) {
            const int lineY = baseY + line.y;
            for (const Segment& segment : line.segments) {
                dc.SetFont(FontFor(segment.style));
                const int segmentX = baseX + segment.x;
                if (segment.style.code && !blockCode) {
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(palette.codeBackground));
                    dc.DrawRectangle(segmentX - 2, lineY - 1,
                                     segment.width + 4, line.height + 2);
                }
                if (segment.style.link) {
                    dc.SetTextForeground(palette.link);
                } else if (segment.style.image) {
                    dc.SetTextForeground(palette.mutedText);
                } else {
                    dc.SetTextForeground(palette.text);
                }
                dc.DrawText(segment.text, segmentX, lineY);
                if (segment.style.strikethrough) {
                    dc.SetPen(wxPen(palette.mutedText, 1));
                    const int strikeY = lineY + line.height / 2;
                    dc.DrawLine(segmentX, strikeY,
                                segmentX + segment.width, strikeY);
                }
            }
        }
    }

    void DrawBlock(wxDC& dc, const BlockLayout& item) {
        const Block& block = *item.block;

        if (block.quoteDepth > 0) {
            dc.SetPen(wxPen(palette.quoteBorder, 3));
            dc.DrawLine(item.x - 13, item.y + 2, item.x - 13,
                        item.y + item.height - 2);
        }

        switch (block.kind) {
            case BlockKind::Rule:
                dc.SetPen(wxPen(palette.border, 1));
                dc.DrawLine(item.x, item.y + item.height / 2,
                            item.x + item.width, item.y + item.height / 2);
                break;
            case BlockKind::Code:
            case BlockKind::Html: {
                dc.SetPen(wxPen(palette.border, 1));
                dc.SetBrush(wxBrush(palette.codeBackground));
                dc.DrawRectangle(item.x, item.y, item.width, item.height);
                const int padding = std::max(6, item.height > 24 ? 10 : 6);
                DrawTextLayout(dc, item.text, item.x + padding,
                               item.y + padding, true);
                break;
            }
            case BlockKind::Table:
                DrawTable(dc, item);
                break;
            default:
                if (!item.marker.empty()) {
                    TextStyle markerStyle;
                    markerStyle.pointSize = 11;
                    markerStyle.bold = block.task;
                    dc.SetFont(FontFor(markerStyle));
                    dc.SetTextForeground(palette.mutedText);
                    dc.DrawText(item.marker, item.markerX, item.markerY);
                }
                DrawTextLayout(dc, item.text, item.x, item.y, false);
                if (block.kind == BlockKind::Heading &&
                    block.headingLevel <= 2) {
                    dc.SetPen(wxPen(palette.border, 1));
                    dc.DrawLine(item.x, item.y + item.height + 3,
                                item.x + item.width,
                                item.y + item.height + 3);
                }
                break;
        }
    }

    void DrawTable(wxDC& dc, const BlockLayout& item) {
        if (item.tableRows.empty()) {
            return;
        }

        dc.SetPen(wxPen(palette.border, 1));
        dc.SetBrush(wxBrush(palette.documentBackground));
        dc.DrawRectangle(item.x, item.y, item.width, item.height);

        if (!item.tableRows.empty()) {
            dc.SetBrush(wxBrush(palette.codeBackground));
            dc.DrawRectangle(item.x + 1, item.y + 1, item.width - 1,
                             item.tableRows.front().height);
        }

        for (const RowLayout& row : item.tableRows) {
            for (const CellLayout& cell : row.cells) {
                DrawTextLayout(dc, cell.text, item.x + cell.x + 7,
                               item.y + row.y + 7, false);
            }
            dc.SetPen(wxPen(palette.border, 1));
            dc.DrawLine(item.x, item.y + row.y, item.x + item.width,
                        item.y + row.y);
        }

        for (const auto& row : item.tableRows) {
            for (const auto& cell : row.cells) {
                dc.SetPen(wxPen(palette.border, 1));
                dc.DrawLine(item.x + cell.x, item.y + row.y,
                            item.x + cell.x,
                            item.y + row.y + row.height);
            }
        }
        dc.DrawLine(item.x + item.width, item.y, item.x + item.width,
                    item.y + item.height);
        dc.DrawLine(item.x, item.y + item.height, item.x + item.width,
                    item.y + item.height);
    }
};

DocumentView::DocumentView(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxBORDER_NONE | wxVSCROLL),
      impl_(std::make_unique<Impl>()) {
    SetScrollRate(0, 1);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &DocumentView::OnPaint, this);
    Bind(wxEVT_SIZE, &DocumentView::OnSize, this);
    Bind(wxEVT_MOUSEWHEEL, &DocumentView::OnMouseWheel, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

DocumentView::~DocumentView() = default;

void DocumentView::ShowPlaceholder() {
    impl_->document.reset();
    impl_->messageTitle = U8("liteMark");
    impl_->messageBody = U8(
        "这是一个只读 Markdown 阅读器。\n"
        "使用“文件 > 打开”或把 Markdown 文件拖入窗口。");
    impl_->layoutDirty = true;
    impl_->pendingScrollY = 0;
    Refresh();
}

void DocumentView::ShowMessage(const wxString& title, const wxString& body) {
    impl_->document.reset();
    impl_->messageTitle = title;
    impl_->messageBody = body;
    impl_->layoutDirty = true;
    impl_->pendingScrollY = 0;
    Refresh();
}

void DocumentView::SetDocument(
    std::shared_ptr<const MarkdownDocument> document, int scrollY) {
    impl_->document = std::move(document);
    impl_->messageTitle.clear();
    impl_->messageBody.clear();
    const int width = GetClientSize().x;
    const auto cached =
        impl_->document ? impl_->layoutCache.find(impl_->document.get())
                        : impl_->layoutCache.end();
    if (cached != impl_->layoutCache.end() &&
        cached->second.layout &&
        cached->second.layout->width == width) {
        impl_->layout = cached->second.layout;
        impl_->layoutWidth = width;
        impl_->layoutDirty = false;
    } else {
        impl_->layoutDirty = true;
    }
    impl_->pendingScrollY = std::max(0, scrollY);
    Refresh();
}

void DocumentView::ForgetDocument(
    const std::shared_ptr<const MarkdownDocument>& document) {
    if (document) {
        impl_->layoutCache.erase(document.get());
    }
}

int DocumentView::ScrollY() const {
    int x = 0;
    int y = 0;
    const_cast<DocumentView*>(this)->GetViewStart(&x, &y);
    return y;
}

void DocumentView::ApplyTheme(const ThemePalette& palette) {
    impl_->palette = palette;
    SetBackgroundColour(palette.documentBackground);
    Refresh();
}

void DocumentView::OnSize(wxSizeEvent& event) {
    if (event.GetSize().x != impl_->layoutWidth) {
        impl_->layoutDirty = true;
    }
    Refresh();
    event.Skip();
}

void DocumentView::OnMouseWheel(wxMouseEvent& event) {
    if (event.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL) {
        event.Skip();
        return;
    }

    const int delta = event.GetWheelDelta() > 0 ? event.GetWheelDelta() : 120;
    const double notches = static_cast<double>(event.GetWheelRotation()) / delta;
    const int pageHeight = GetClientSize().y;
    const int distance = static_cast<int>(
        std::lround(pageHeight * kMouseWheelPageFraction * notches));

    if (distance != 0) {
        int x = 0;
        int y = 0;
        GetViewStart(&x, &y);
        Scroll(0, std::max(0, y - distance));
        Refresh(false);
    }
}

void DocumentView::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    DoPrepareDC(dc);

    int scrollX = 0;
    int scrollY = 0;
    GetViewStart(&scrollX, &scrollY);
    const wxSize client = GetClientSize();

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(impl_->palette.documentBackground));
    dc.DrawRectangle(scrollX, scrollY, client.x, client.y);

    if (impl_->document == nullptr) {
        SetVirtualSize(client);
        impl_->DrawPlaceholder(dc, scrollX, scrollY, client.x);
        return;
    }

    if (!impl_->layout || impl_->layoutDirty ||
        impl_->layoutWidth != client.x) {
        impl_->InitializeLayout(dc, client.x);
    }

    const int pageMargin = impl_->PageMargin(dc);
    const int viewBottom = scrollY + client.y;
    const int layoutStart = std::max(0, scrollY - client.y);
    const int layoutEnd = viewBottom + client.y;
    int y = pageMargin;

    for (std::size_t i = 0; i < impl_->layout->slots.size(); ++i) {
        const int height = impl_->SlotHeight(impl_->layout->slots[i]);
        if (y + height < layoutStart) {
            y += height;
            continue;
        }
        if (y > layoutEnd) {
            break;
        }

        const BlockLayout& blockLayout =
            impl_->LayoutFor(dc, i, y, client.x);
        if (blockLayout.y + blockLayout.height >= scrollY &&
            blockLayout.y <= viewBottom) {
            impl_->DrawBlock(dc, blockLayout);
        }
        y += impl_->SlotHeight(impl_->layout->slots[i]);
    }

    const int totalHeight = impl_->TotalHeight() + pageMargin * 2;
    SetVirtualSize(wxSize(client.x, std::max(client.y, totalHeight)));
    if (impl_->pendingScrollY > 0) {
        const int target = impl_->pendingScrollY;
        impl_->pendingScrollY = 0;
        CallAfter([this, target]() { Scroll(0, target); });
    }
}

}  // namespace litemark
