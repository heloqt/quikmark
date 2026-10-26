#include "ui/DocumentView.h"

#include "core/document/DocumentNavigation.h"
#include "core/document/DocumentText.h"
#include "core/layout/ListLayout.h"
#include "core/policy/ResourcePolicy.h"
#include "platform/PerformanceTrace.h"
#include "ui/BlockLayout.h"
#include "ui/DocumentMediaRenderer.h"
#include "ui/PendingScroll.h"
#include "ui/StatusIconRenderer.h"
#include "ui/StatusIcons.h"
#include "ui/TableLayout.h"
#include "ui/TextLayout.h"

#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/cursor.h>
#include <wx/dataobj.h>
#include <wx/dc.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/event.h>
#include <wx/filename.h>
#include <wx/font.h>
#include <wx/msgdlg.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include <wx/utils.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using litemark::detail::BaseStyle;
using litemark::detail::BlockLayout;
using litemark::detail::BlockSlot;
using litemark::detail::BuildFont;
using litemark::detail::BuildTableLayout;
using litemark::detail::BuildTextLayout;
using litemark::detail::CellLayout;
using litemark::detail::DocumentMediaRenderer;
using litemark::detail::FontMap;
using litemark::detail::FlowchartLoadedPayload;
using litemark::detail::ImageLoadedPayload;
using litemark::detail::IntrinsicAfter;
using litemark::detail::IntrinsicBefore;
using litemark::detail::LayoutCacheEntry;
using litemark::detail::Line;
using litemark::detail::LineHeight;
using litemark::detail::RowLayout;
using litemark::detail::ScaledPointSize;
using litemark::detail::Segment;
using litemark::detail::StatusIconDrawCommand;
using litemark::detail::StatusIconRun;
using litemark::detail::TextLayout;
using litemark::detail::TextStyle;
using litemark::detail::Utf8CodeUnitAdvance;
using litemark::detail::ViewLayout;

namespace {

constexpr double kMouseWheelPageFraction = 0.45;
constexpr double kFontZoomStep = 0.10;
constexpr double kMinFontScale = 0.75;
constexpr double kMaxFontScale = 2.00;
constexpr int kImageLoadedEventId = wxID_HIGHEST + 501;
constexpr int kFlowchartLoadedEventId = wxID_HIGHEST + 502;
#ifdef __WXMSW__
constexpr UINT kMediaImageCountMessage = WM_APP + 711;
constexpr UINT kMediaFlowchartCountMessage = WM_APP + 712;
#endif

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

bool IsDarkPalette(const litemark::ThemePalette& palette) {
    return palette.documentBackground.Red() < 100;
}

wxColour SearchHighlightColour(const litemark::ThemePalette& palette,
                               bool active) {
    if (IsDarkPalette(palette)) {
        return active ? wxColour("#7A5D00") : wxColour("#4A3B00");
    }
    return active ? wxColour("#FFD666") : wxColour("#FFF3B0");
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

enum class ExternalLinkResult {
    Opened,
    Unsupported,
    Failed
};

ExternalLinkResult OpenExternalLink(const wxString& href) {
    const wxScopedCharBuffer utf8 = href.ToUTF8();
    const litemark::ResourceReferenceKind kind =
        litemark::ClassifyResourceReference(
        std::string_view(utf8.data(), utf8.length()));
    bool opened = false;
    if (kind == litemark::ResourceReferenceKind::Http ||
        kind == litemark::ResourceReferenceKind::Https) {
        opened = wxLaunchDefaultBrowser(href);
    } else if (kind == litemark::ResourceReferenceKind::Mailto) {
        opened = wxLaunchDefaultApplication(href);
    } else {
        return ExternalLinkResult::Unsupported;
    }
    return opened ? ExternalLinkResult::Opened : ExternalLinkResult::Failed;
}

}  // namespace

namespace litemark {

struct DocumentView::Impl {
    struct ReadingAnchor {
        std::size_t blockIndex = 0;
        int intraBlockY = 0;
    };

    struct PendingPaintTrace {
        std::string event;
        std::optional<std::chrono::steady_clock::time_point> start;
        bool processAge = false;
    };

    DocumentView* owner = nullptr;
    std::unique_ptr<DocumentMediaRenderer> media;
    std::shared_ptr<const MarkdownDocument> document;
    ThemePalette palette;
    std::shared_ptr<ViewLayout> layout;
    bool layoutDirty = true;
    int layoutWidth = 0;
    PendingScrollPosition pendingScrollY;
    std::optional<ReadingAnchor> pendingReadingAnchor;
    bool suppressSizeReadingAnchor = false;
    double fontScale = 1.0;
    double zoomWheelAccumulator = 0.0;
    FontMap fonts;
    std::map<const MarkdownDocument*, LayoutCacheEntry> layoutCache;
    wxString messageTitle = U8("liteMark");
    wxString messageBody = U8(
        "这是一个只读 Markdown 阅读器。\n"
        "使用“文件 > 打开”或把 Markdown 文件拖入窗口。");
    BlockLayout* tableDrag = nullptr;
    std::optional<std::chrono::steady_clock::time_point> scrollRequestedAt;
    std::vector<PendingPaintTrace> pendingPaintTraces;
    std::vector<litemark::HeadingAnchor> headingAnchors;
    bool headingAnchorsBuilt = false;
    std::optional<litemark::TextSelectionPoint> selectionAnchor;
    std::optional<litemark::TextSelectionPoint> selectionFocus;
    std::vector<litemark::SearchMatch> searchMatches;
    int activeSearchMatch = -1;
    bool selecting = false;
    bool selectionMoved = false;
    wxString pressedLink;

    static int TableMaxScroll(const BlockLayout& item) {
        return std::max(0, item.tableContentWidth - item.width);
    }

    static int TableThumbWidth(const BlockLayout& item) {
        if (item.tableContentWidth <= 0) {
            return item.width;
        }
        return std::max(28, item.width * item.width / item.tableContentWidth);
    }

    BlockLayout* TableAt(const wxPoint& point) {
        if (!layout) {
            return nullptr;
        }
        for (auto& [index, item] : layout->blockLayouts) {
            (void)index;
            if (item.block->kind != BlockKind::Table) {
                continue;
            }
            if (point.x >= item.x && point.x <= item.x + item.width &&
                point.y >= item.y &&
                point.y <= item.y + item.tableRowsHeight +
                               item.tableScrollbarHeight) {
                return &item;
            }
        }
        return nullptr;
    }

    wxString LinkAtInText(const TextLayout& text, int baseX, int baseY,
                          const wxPoint& point) const {
        for (const Line& line : text.lines) {
            const int lineY = baseY + line.y;
            if (point.y < lineY || point.y > lineY + line.height) {
                continue;
            }
            for (const Segment& segment : line.segments) {
                const int segmentX = baseX + segment.x;
                if (!segment.href.empty() && segment.style.link &&
                    point.x >= segmentX &&
                    point.x <= segmentX + segment.width) {
                    return segment.href;
                }
            }
        }
        return {};
    }

    wxString LinkAt(const wxPoint& point) {
        if (!layout) {
            return {};
        }
        const int padding = FromDIPValue(9);
        for (const auto& [index, item] : layout->blockLayouts) {
            (void)index;
            if (item.block->kind == BlockKind::Table) {
                if (point.x < item.x || point.x > item.x + item.width ||
                    point.y < item.y ||
                    point.y > item.y + item.tableRowsHeight) {
                    continue;
                }
                for (const RowLayout& row : item.tableRows) {
                    for (const CellLayout& cell : row.cells) {
                        const wxString href = LinkAtInText(
                            cell.text,
                            item.x + cell.x - item.tableScrollX + padding,
                            item.y + row.y + padding, point);
                        if (!href.empty()) {
                            return href;
                        }
                    }
                }
            } else {
                const wxString href =
                    LinkAtInText(item.text, item.x, item.y, point);
                if (!href.empty()) {
                    return href;
                }
            }
        }
        return {};
    }

    int FromDIPValue(int value) const {
        const double scale = owner != nullptr ? owner->GetDPIScaleFactor() : 1.0;
        return static_cast<int>(std::lround(value * scale));
    }

    void SetTableScrollFromX(BlockLayout& item, int x) {
        const int maxScroll = TableMaxScroll(item);
        if (maxScroll <= 0) {
            item.tableScrollX = 0;
            return;
        }
        const int thumbWidth = TableThumbWidth(item);
        const int travel = std::max(1, item.width - thumbWidth);
        const int thumbX =
            std::clamp(x - item.x - thumbWidth / 2, 0, travel);
        item.tableScrollX = thumbX * maxScroll / travel;
    }

    wxFont FontFor(const TextStyle& style) {
        TextStyle scaled = style;
        scaled.pointSize = ScaledPointSize(style.pointSize, fontScale);
        auto result = fonts.try_emplace(scaled.key(), BuildFont(scaled));
        return result.first->second;
    }

    double Scale(wxDC& dc) const {
        return static_cast<double>(dc.GetPPI().x) / 96.0;
    }

    int PageMargin(wxDC& dc) const {
        return static_cast<int>(std::lround(34 * Scale(dc)));
    }

    int EstimateHeight(const Block& block, int width, double scale) const {
        const int before =
            static_cast<int>(std::lround(IntrinsicBefore(block) * scale));
        const int after =
            static_cast<int>(std::lround(IntrinsicAfter(block) * scale));

        if (block.kind == BlockKind::Rule) {
            return before + static_cast<int>(std::lround(18 * scale)) + after;
        }
        if (block.kind == BlockKind::Image) {
            return before + static_cast<int>(std::lround(130 * scale)) +
                   after;
        }
        if (block.kind == BlockKind::Flowchart) {
            return before + static_cast<int>(std::lround(150 * scale)) +
                   after;
        }
        if (block.kind == BlockKind::Table) {
            const int rows = std::max(1, static_cast<int>(block.tableRows.size()));
            return before +
                   rows * static_cast<int>(
                              std::lround(30 * scale * fontScale)) +
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
            std::max(6, static_cast<int>(
                            std::lround(9 * scale * fontScale)));
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
            static_cast<int>(std::lround(
                (lines * lineHeight * fontScale + 12) * scale));
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
        const double scale = Scale(dc);
        layout->slots.reserve(document->blocks.size());
        for (const Block& block : document->blocks) {
            const int quoteIndent = static_cast<int>(
                std::lround(block.quoteDepth * 18 * scale));
            const ListLayoutMetrics listLayout = ComputeListLayout(
                block.listDepth, block.listOrdered, block.listIndex,
                scale, fontScale);
            const int width =
                std::max(80, maxContentWidth - quoteIndent -
                                  listLayout.textOffset);
            const int estimatedHeight = EstimateHeight(block, width, scale);
            layout->slots.push_back(
                BlockSlot{estimatedHeight, -1});
            layout->estimatedTotalHeight += estimatedHeight;
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
        return layout ? layout->estimatedTotalHeight : 0;
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
        const ListLayoutMetrics listLayout = ComputeListLayout(
            block.listDepth, block.listOrdered, block.listIndex,
            scale, fontScale);
        const int listMarkerX =
            pageMargin + quoteIndent + listLayout.markerOffset;
        const int x =
            pageMargin + quoteIndent + listLayout.textOffset;
        const int width =
            std::max(80, maxContentWidth - quoteIndent -
                              listLayout.textOffset);
        const int before =
            static_cast<int>(std::lround(IntrinsicBefore(block) * scale));
        const int after =
            static_cast<int>(std::lround(IntrinsicAfter(block) * scale));

        BlockLayout item;
        item.block = &block;
        item.blockIndex = index;
        item.x = x;
        item.y = y + before;
        item.width = width;
        if (block.listMarker) {
            item.markerX = listMarkerX;
            item.markerGutter = listLayout.markerGutter;
            item.markerY = item.y;
        }

        if (block.kind == BlockKind::Rule) {
            item.height = static_cast<int>(std::lround(18 * scale));
        } else if (block.kind == BlockKind::Image) {
            media->BuildImageLayout(dc, item, width, scale);
        } else if (block.kind == BlockKind::Flowchart) {
            media->BuildFlowchartLayout(
                dc, item, width, scale, fontScale, &fonts);
        } else if (block.kind == BlockKind::Table) {
            BuildTableLayout(
                dc, block, item, width, scale, fontScale, &fonts);
        } else if (block.kind == BlockKind::Code ||
                   block.kind == BlockKind::Html) {
            TextStyle style = BaseStyle(block);
            std::vector<InlineSpan> raw(1);
            raw[0].text = block.raw;
            const int padding = static_cast<int>(std::lround(10 * scale));
            BuildTextLayout(dc, item.text, width - padding * 2, raw, style,
                            fontScale, &fonts);
            item.height = item.text.height + padding * 2;
        } else {
            TextStyle style = BaseStyle(block);
            BuildTextLayout(dc, item.text, width, block.spans,
                            style, fontScale, &fonts);
            item.height = item.text.height;
            if (block.listMarker) {
                item.marker = BlockMarker(block);
            }
        }

        const int actualHeight = before + item.height + after;
        BlockSlot& slot = layout->slots[index];
        const int previousHeight = SlotHeight(slot);
        slot.actualHeight = std::max(1, actualHeight);
        layout->estimatedTotalHeight += slot.actualHeight - previousHeight;
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

    std::size_t SourceOffsetAtX(wxDC& dc, const Segment& segment,
                                int localX) {
        dc.SetFont(FontFor(segment.style));
        wxArrayInt partial;
        dc.GetPartialTextExtents(segment.text, partial);
        const int clamped =
            std::clamp(localX, 0, std::max(0, segment.width));
        std::size_t best = 0;
        int bestDistance = std::abs(clamped);
        for (std::size_t index = 0; index < partial.size(); ++index) {
            const int distance = std::abs(clamped - partial[index]);
            if (distance < bestDistance) {
                best = index + 1;
                bestDistance = distance;
            }
        }
        std::size_t sourceOffset = segment.sourceStart;
        for (std::size_t index = 0; index < best; ++index) {
            sourceOffset += Utf8CodeUnitAdvance(
                segment.text, static_cast<int>(index));
        }
        return std::min(sourceOffset, segment.sourceEnd);
    }

    int XAtSourceOffset(wxDC& dc, const Segment& segment,
                        std::size_t sourceOffset) {
        const std::size_t target = std::max(
            segment.sourceStart,
            std::min(sourceOffset, segment.sourceEnd));
        std::size_t sourceOffsetCursor = segment.sourceStart;
        std::size_t visibleLength = 0;
        while (visibleLength < segment.text.length() &&
               sourceOffsetCursor < target) {
            sourceOffsetCursor += Utf8CodeUnitAdvance(
                segment.text, static_cast<int>(visibleLength));
            ++visibleLength;
        }
        if (visibleLength == 0) {
            return 0;
        }
        dc.SetFont(FontFor(segment.style));
        return dc.GetTextExtent(
                     segment.text.Left(static_cast<int>(visibleLength)))
            .x;
    }

    std::optional<std::pair<std::size_t, std::size_t>> SelectionForBlock(
        std::size_t blockIndex) const {
        if (!document || !selectionAnchor || !selectionFocus) {
            return std::nullopt;
        }
        const TextSelectionRange normalized = NormalizeSelection(
            *document, TextSelectionRange{*selectionAnchor, *selectionFocus});
        if (IsEmptySelection(*document, normalized) ||
            blockIndex < normalized.start.blockIndex ||
            blockIndex > normalized.end.blockIndex) {
            return std::nullopt;
        }
        const std::size_t fullSize =
            BlockPlainText(document->blocks[blockIndex]).size();
        const std::size_t begin =
            blockIndex == normalized.start.blockIndex
                ? normalized.start.utf8Offset
                : 0;
        const std::size_t end =
            blockIndex == normalized.end.blockIndex
                ? normalized.end.utf8Offset
                : fullSize;
        if (begin >= end) {
            return std::nullopt;
        }
        return std::make_pair(begin, end);
    }

    std::vector<std::pair<std::size_t, std::size_t>>
    SearchRangesForBlock(std::size_t blockIndex) const {
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
        for (const auto& match : searchMatches) {
            if (match.blockIndex == blockIndex) {
                ranges.emplace_back(match.utf8Offset,
                                    match.utf8Offset + match.utf8Length);
            }
        }
        return ranges;
    }

    std::optional<std::pair<std::size_t, std::size_t>>
    ActiveSearchRangeForBlock(std::size_t blockIndex) const {
        if (activeSearchMatch < 0 ||
            static_cast<std::size_t>(activeSearchMatch) >=
                searchMatches.size()) {
            return std::nullopt;
        }
        const SearchMatch& match =
            searchMatches[static_cast<std::size_t>(activeSearchMatch)];
        if (match.blockIndex != blockIndex) {
            return std::nullopt;
        }
        return std::make_pair(match.utf8Offset,
                              match.utf8Offset + match.utf8Length);
    }

    std::optional<TextSelectionPoint> HitTestPosition(
        wxDC& dc, const wxPoint& point) {
        if (!document || document->blocks.empty()) {
            return std::nullopt;
        }
        if (!layout) {
            return TextSelectionPoint{0, 0};
        }

        std::size_t blockIndex = document->blocks.size() - 1;
        int blockY = PageMargin(dc);
        for (std::size_t index = 0; index < layout->slots.size(); ++index) {
            const int height = SlotHeight(layout->slots[index]);
            if (point.y < blockY + height) {
                blockIndex = index;
                break;
            }
            blockY += height;
        }

        BlockLayout& item = LayoutFor(
            dc, blockIndex, blockY, owner->GetClientSize().x);
        const Block& block = *item.block;
        const std::string fullText = BlockPlainText(block);
        const bool textBlock =
            block.kind == BlockKind::Text ||
            block.kind == BlockKind::Heading ||
            block.kind == BlockKind::Code ||
            block.kind == BlockKind::Html;
        if (!textBlock || item.text.lines.empty()) {
            const std::size_t offset =
                point.y > item.y + item.height / 2 ? fullText.size() : 0;
            return TextSelectionPoint{blockIndex, offset};
        }

        const bool code =
            block.kind == BlockKind::Code || block.kind == BlockKind::Html;
        const int padding =
            code ? std::max(6, item.height > 24 ? 10 : 6) : 0;
        const int baseX = item.x + padding;
        const int baseY = item.y + padding;
        const int relativeY = point.y - baseY;
        const Line* selectedLine = &item.text.lines.front();
        int closestDistance = std::abs(relativeY - selectedLine->y);
        for (const auto& line : item.text.lines) {
            if (relativeY >= line.y && relativeY < line.y + line.height) {
                selectedLine = &line;
                break;
            }
            const int distance =
                std::abs(relativeY - (line.y + line.height / 2));
            if (distance < closestDistance) {
                selectedLine = &line;
                closestDistance = distance;
            }
        }
        if (selectedLine->segments.empty()) {
            return TextSelectionPoint{blockIndex, 0};
        }

        const int localX = point.x - baseX;
        for (const auto& segment : selectedLine->segments) {
            if (localX <= segment.x + segment.width) {
                return TextSelectionPoint{
                    blockIndex,
                    std::min(fullText.size(),
                             SourceOffsetAtX(
                                 dc, segment, localX - segment.x))};
            }
        }
        return TextSelectionPoint{
            blockIndex,
            std::min(fullText.size(),
                     selectedLine->segments.back().sourceEnd)};
    }

    std::optional<ReadingAnchor> CaptureReadingAnchor() const {
        if (!layout || layout->slots.empty()) {
            return std::nullopt;
        }
        const int scrollY = owner->ScrollY();
        int blockY = owner->FromDIP(34);
        for (std::size_t index = 0; index < layout->slots.size(); ++index) {
            const int height = SlotHeight(layout->slots[index]);
            if (scrollY < blockY + height) {
                return ReadingAnchor{
                    index, std::max(0, scrollY - blockY)};
            }
            blockY += height;
        }
        const std::size_t last = layout->slots.size() - 1;
        return ReadingAnchor{
            last, std::max(0, SlotHeight(layout->slots[last]) - 1)};
    }

    int ReadingAnchorScrollY(const ReadingAnchor& anchor) const {
        if (!layout || layout->slots.empty()) {
            return 0;
        }
        const std::size_t index =
            std::min(anchor.blockIndex, layout->slots.size() - 1);
        int blockY = owner->FromDIP(34);
        for (std::size_t current = 0; current < index; ++current) {
            blockY += SlotHeight(layout->slots[current]);
        }
        return blockY +
               std::min(anchor.intraBlockY,
                        std::max(0, SlotHeight(layout->slots[index]) - 1));
    }

    void ClearSelection() {
        selectionAnchor.reset();
        selectionFocus.reset();
        selecting = false;
        selectionMoved = false;
        pressedLink.clear();
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

    void DrawPlaceholder(
        wxDC& dc, int scrollX, int scrollY, int clientWidth,
        std::vector<StatusIconDrawCommand>* iconCommands) {
        TextStyle titleStyle;
        titleStyle.pointSize = 24;
        titleStyle.bold = true;
        TextStyle bodyStyle;
        bodyStyle.pointSize = 11;

        TextLayout title;
        std::vector<InlineSpan> titleSpan(1);
        titleSpan[0].text = messageTitle.ToUTF8().data();
        BuildTextLayout(dc, title, clientWidth - 80, titleSpan, titleStyle,
                        fontScale, &fonts);

        TextLayout body;
        std::vector<InlineSpan> bodySpans(1);
        bodySpans[0].text = messageBody.ToUTF8().data();
        BuildTextLayout(dc, body, clientWidth - 80, bodySpans, bodyStyle,
                        fontScale, &fonts);

        const int x = scrollX + 40;
        int y = scrollY + 36;
        DrawTextLayout(dc, title, x, y, false, std::nullopt, nullptr,
                       nullptr, iconCommands);
        y += title.height + 18;
        DrawTextLayout(dc, body, x, y, false, std::nullopt, nullptr,
                       nullptr, iconCommands);
    }

    void DrawTextLayout(wxDC& dc, const TextLayout& text, int baseX,
                        int baseY, bool blockCode,
                        const std::optional<std::pair<std::size_t,
                                                      std::size_t>>&
                            selection = std::nullopt,
                        const std::vector<std::pair<std::size_t,
                                                    std::size_t>>*
                            searchRanges = nullptr,
                        const std::optional<std::pair<std::size_t,
                                                      std::size_t>>*
                            activeSearch = nullptr,
                        std::vector<StatusIconDrawCommand>* iconCommands =
                            nullptr) {
        const wxColour searchColour =
            SearchHighlightColour(palette, false);
        const wxColour activeSearchColour =
            SearchHighlightColour(palette, true);
        int clipX = 0;
        int clipY = 0;
        int clipWidth = 0;
        int clipHeight = 0;
        const bool hasClip =
            dc.GetClippingBox(&clipX, &clipY, &clipWidth, &clipHeight);
        auto drawRange = [&](const Segment& segment, int segmentX,
                             int lineY, int lineHeight,
                             const std::pair<std::size_t, std::size_t>& range,
                             const wxColour& colour) {
            if (segment.sourceEnd <= segment.sourceStart) {
                return;
            }
            const std::size_t begin =
                std::max(range.first, segment.sourceStart);
            const std::size_t end =
                std::min(range.second, segment.sourceEnd);
            if (begin >= end) {
                return;
            }
            const int x0 = XAtSourceOffset(dc, segment, begin);
            const int x1 = XAtSourceOffset(dc, segment, end);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(colour));
            dc.DrawRectangle(segmentX + x0, lineY - 1,
                             std::max(1, x1 - x0), lineHeight + 2);
        };

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
                if (searchRanges != nullptr) {
                    for (const auto& range : *searchRanges) {
                        drawRange(segment, segmentX, lineY, line.height,
                                  range, searchColour);
                    }
                }
                if (activeSearch) {
                    drawRange(segment, segmentX, lineY, line.height,
                              **activeSearch, activeSearchColour);
                }
                if (selection) {
                    drawRange(segment, segmentX, lineY, line.height,
                              *selection, palette.selection);
                }
                wxColour textColour;
                if (segment.style.link) {
                    textColour = palette.link;
                } else if (segment.style.image) {
                    textColour = palette.mutedText;
                } else {
                    textColour = palette.text;
                }
                const wxFont& font = FontFor(segment.style);
                bool drewText = false;
#ifdef __WXMSW__
                if (iconCommands != nullptr && segment.statusIcon) {
                    dc.SetFont(font);
                    dc.SetTextForeground(textColour);
                    int previousWidth = 0;
                    for (const StatusIconRun& run :
                         litemark::detail::SplitStatusIcons(segment.text)) {
                        const wxString runText = segment.text.Mid(
                            run.begin, run.end - run.begin);
                        const int endWidth =
                            dc.GetTextExtent(
                                  segment.text.Left(run.end))
                                .x;
                        const int runWidth =
                            std::max(0, endWidth - previousWidth);
                        if (run.icon) {
                            StatusIconDrawCommand command;
                            command.id = run.id;
                            command.fallbackText = runText;
                            command.fallbackFont = font;
                            command.colour = textColour;
                            command.x = segmentX + previousWidth;
                            command.y = lineY;
                            command.width = runWidth;
                            command.height = line.height;
                            if (hasClip) {
                                command.clipX = clipX;
                                command.clipY = clipY;
                                command.clipWidth = clipWidth;
                                command.clipHeight = clipHeight;
                            }
                            iconCommands->push_back(
                                std::move(command));
                        } else {
                            dc.DrawText(runText,
                                        segmentX + previousWidth,
                                        lineY);
                        }
                        previousWidth = endWidth;
                    }
                    drewText = true;
                }
#endif
                if (!drewText) {
                    dc.SetFont(font);
                    dc.SetTextForeground(textColour);
                    dc.DrawText(segment.text, segmentX, lineY);
                }
                if (segment.style.strikethrough) {
                    dc.SetPen(wxPen(palette.mutedText, 1));
                    const int strikeY = lineY + line.height / 2;
                    dc.DrawLine(segmentX, strikeY,
                                segmentX + segment.width, strikeY);
                }
            }
        }
    }

    void DrawBlock(
        wxDC& dc, const BlockLayout& item,
        std::vector<StatusIconDrawCommand>* iconCommands) {
        const Block& block = *item.block;
        const auto selection = SelectionForBlock(item.blockIndex);
        const auto searchRanges = SearchRangesForBlock(item.blockIndex);
        const auto activeSearch =
            ActiveSearchRangeForBlock(item.blockIndex);
        const auto* searchRangesPtr =
            searchRanges.empty() ? nullptr : &searchRanges;
        const auto* activeSearchPtr =
            activeSearch ? &activeSearch : nullptr;

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
                               item.y + padding, true,
                               selection, searchRangesPtr, activeSearchPtr,
                               iconCommands);
                break;
            }
            case BlockKind::Table:
                DrawTable(dc, item, iconCommands);
                break;
            case BlockKind::Image:
                media->DrawImage(dc, item, palette, fontScale);
                break;
            case BlockKind::Flowchart:
                media->DrawFlowchart(
                    dc, item, palette, fontScale,
                    [this, iconCommands](
                        wxDC& target, const TextLayout& text,
                        int x, int y) {
                        DrawTextLayout(target, text, x, y, false,
                                       std::nullopt, nullptr, nullptr,
                                       iconCommands);
                    });
                break;
            default:
                if (!item.marker.empty()) {
                    TextStyle markerStyle;
                    markerStyle.pointSize = 11;
                    markerStyle.bold = block.task;
                    dc.SetFont(FontFor(markerStyle));
                    dc.SetTextForeground(palette.mutedText);
                    int markerX = item.markerX;
                    if (block.listOrdered) {
                        const int markerWidth =
                            dc.GetTextExtent(item.marker).x;
                        markerX += std::max(
                            0, item.markerGutter - FromDIPValue(5) -
                                   markerWidth);
                    }
                    dc.DrawText(item.marker, markerX, item.markerY);
                }
                DrawTextLayout(dc, item.text, item.x, item.y, false,
                               selection, searchRangesPtr, activeSearchPtr,
                               iconCommands);
                if (block.kind == BlockKind::Heading &&
                    block.headingLevel <= 2) {
                    dc.SetPen(wxPen(palette.border, 1));
                    dc.DrawLine(item.x, item.y + item.height + 3,
                                item.x + item.width,
                                item.y + item.height + 3);
                }
                break;
        }
        if (selection &&
            block.kind != BlockKind::Text &&
            block.kind != BlockKind::Heading &&
            block.kind != BlockKind::Code &&
            block.kind != BlockKind::Html) {
            dc.SetPen(wxPen(palette.accent, 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(item.x - 2, item.y - 2,
                             item.width + 4, item.height + 4);
        }
        if (activeSearchPtr != nullptr &&
            block.kind != BlockKind::Text &&
            block.kind != BlockKind::Heading &&
            block.kind != BlockKind::Code &&
            block.kind != BlockKind::Html) {
            dc.SetPen(wxPen(SearchHighlightColour(palette, true), 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(item.x - 2, item.y - 2,
                             item.width + 4, item.height + 4);
        }
    }

    void DrawTable(
        wxDC& dc, const BlockLayout& item,
        std::vector<StatusIconDrawCommand>* iconCommands) {
        if (item.tableRows.empty()) {
            return;
        }
        const Block& block = *item.block;

        const int padding = std::max(5, static_cast<int>(std::lround(
                                           9 * (dc.GetPPI().x / 96.0))));
        const int scrollX = item.tableScrollX;
        dc.SetClippingRegion(item.x, item.y, item.width,
                             item.tableRowsHeight);
        dc.SetPen(wxPen(palette.border, 1));
        dc.SetBrush(wxBrush(palette.documentBackground));
        dc.DrawRectangle(item.x, item.y, item.width, item.tableRowsHeight);

        for (std::size_t r = 0; r < item.tableRows.size(); ++r) {
            const RowLayout& row = item.tableRows[r];
            const bool header =
                block.tableFirstRowIsHeader && r == 0;
            if (header) {
                dc.SetBrush(wxBrush(palette.codeBackground));
                dc.DrawRectangle(item.x, item.y + row.y, item.width,
                                 row.height);
            } else if ((block.tableRowOffset + r) % 2 == 0) {
                dc.SetBrush(wxBrush(palette.windowBackground));
                dc.DrawRectangle(item.x, item.y + row.y, item.width,
                                 row.height);
            }
        }

        for (const RowLayout& row : item.tableRows) {
            for (const CellLayout& cell : row.cells) {
                const int cellX = item.x + cell.x - scrollX;
                if (cellX + cell.width < item.x ||
                    cellX > item.x + item.width) {
                    continue;
                }
                DrawTextLayout(dc, cell.text, cellX + padding,
                               item.y + row.y + padding, false,
                               std::nullopt, nullptr, nullptr,
                               iconCommands);
            }
        }

        for (std::size_t r = 0; r < item.tableRows.size(); ++r) {
            const RowLayout& row = item.tableRows[r];
            const int rowBottom = item.y + row.y + row.height;
            if (block.tableFirstRowIsHeader && r == 0) {
                dc.SetPen(wxPen(palette.border, 2));
            } else {
                dc.SetPen(wxPen(palette.border, 1));
            }
            dc.DrawLine(item.x, rowBottom, item.x + item.width, rowBottom);

            for (std::size_t c = 0; c + 1 < row.cells.size(); ++c) {
                const int separatorX =
                    item.x + row.cells[c].x + row.cells[c].width - scrollX;
                if (separatorX > item.x && separatorX < item.x + item.width) {
                    dc.SetPen(wxPen(palette.border, 1));
                    dc.DrawLine(separatorX, item.y + row.y, separatorX,
                                rowBottom);
                }
            }
        }
        dc.DestroyClippingRegion();

        dc.SetPen(wxPen(palette.border, 1));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(item.x, item.y, item.width, item.tableRowsHeight);

        if (item.tableScrollbarHeight > 0) {
            const int trackY = item.y + item.tableRowsHeight;
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(palette.codeBackground));
            dc.DrawRectangle(item.x, trackY + 1, item.width,
                             item.tableScrollbarHeight - 1);

            const int thumbWidth = std::max(
                static_cast<int>(std::lround(28 * (dc.GetPPI().x / 96.0))),
                item.width * item.width / item.tableContentWidth);
            const int maxScroll = item.tableContentWidth - item.width;
            const int travel = std::max(0, item.width - thumbWidth);
            const int thumbX =
                item.x + (maxScroll > 0 ? travel * item.tableScrollX / maxScroll
                                        : 0);
            dc.SetBrush(wxBrush(palette.mutedText));
            const int thumbHeight = std::max(3, item.tableScrollbarHeight - 4);
            dc.DrawRoundedRectangle(thumbX + 2, trackY + 2,
                                    std::max(1, thumbWidth - 4), thumbHeight,
                                    thumbHeight / 2);
        }
    }
};

DocumentView::DocumentView(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxBORDER_NONE | wxVSCROLL),
      impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->media = std::make_unique<DocumentMediaRenderer>(
        this, kImageLoadedEventId, kFlowchartLoadedEventId);
    SetScrollRate(0, 1);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &DocumentView::OnPaint, this);
    Bind(wxEVT_SIZE, &DocumentView::OnSize, this);
    Bind(wxEVT_MOUSEWHEEL, &DocumentView::OnMouseWheel, this);
    Bind(wxEVT_LEFT_DOWN, &DocumentView::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &DocumentView::OnLeftUp, this);
    Bind(wxEVT_MOTION, &DocumentView::OnMouseMove, this);
    Bind(wxEVT_CHAR_HOOK, &DocumentView::OnCharHook, this);
    Bind(wxEVT_THREAD, &DocumentView::OnImageLoaded, this,
         kImageLoadedEventId);
    Bind(wxEVT_THREAD, &DocumentView::OnFlowchartLoaded, this,
         kFlowchartLoadedEventId);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

DocumentView::~DocumentView() = default;

#ifdef __WXMSW__
WXLRESULT DocumentView::MSWWindowProc(WXUINT message, WXWPARAM wParam,
                                      WXLPARAM lParam) {
    if (message == WM_COPY) {
        CopySelectionToClipboard();
        return 0;
    }
    if (message == kMediaImageCountMessage) {
        return static_cast<WXLRESULT>(
            impl_->media->LoadedImageCount());
    }
    if (message == kMediaFlowchartCountMessage) {
        return static_cast<WXLRESULT>(
            impl_->media->LoadedFlowchartCount());
    }
    return wxScrolledWindow::MSWWindowProc(message, wParam, lParam);
}
#endif

void DocumentView::ShowPlaceholder() {
    impl_->tableDrag = nullptr;
    impl_->ClearSelection();
    impl_->document.reset();
    impl_->headingAnchors.clear();
    impl_->headingAnchorsBuilt = false;
    impl_->searchMatches.clear();
    impl_->activeSearchMatch = -1;
    impl_->media->SetBasePath({});
    impl_->messageTitle = U8("liteMark");
    impl_->messageBody = U8(
        "这是一个只读 Markdown 阅读器。\n"
        "使用“文件 > 打开”或把 Markdown 文件拖入窗口。");
    impl_->layoutDirty = true;
    impl_->pendingScrollY.Request(0);
    impl_->pendingReadingAnchor.reset();
    impl_->suppressSizeReadingAnchor = false;
    Refresh();
}

void DocumentView::ShowMessage(const wxString& title, const wxString& body) {
    impl_->tableDrag = nullptr;
    impl_->ClearSelection();
    impl_->document.reset();
    impl_->headingAnchors.clear();
    impl_->headingAnchorsBuilt = false;
    impl_->searchMatches.clear();
    impl_->activeSearchMatch = -1;
    impl_->media->SetBasePath({});
    impl_->messageTitle = title;
    impl_->messageBody = body;
    impl_->layoutDirty = true;
    impl_->pendingScrollY.Request(0);
    impl_->pendingReadingAnchor.reset();
    impl_->suppressSizeReadingAnchor = false;
    Refresh();
}

void DocumentView::SetDocument(
    std::shared_ptr<const MarkdownDocument> document,
    const wxString& basePath, int scrollY) {
    impl_->tableDrag = nullptr;
    impl_->ClearSelection();
    impl_->document = std::move(document);
    impl_->headingAnchors.clear();
    impl_->headingAnchorsBuilt = false;
    impl_->searchMatches.clear();
    impl_->activeSearchMatch = -1;
    impl_->media->SetBasePath(basePath);
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
    impl_->pendingScrollY.Request(scrollY);
    impl_->pendingReadingAnchor.reset();
    impl_->suppressSizeReadingAnchor = true;
    Refresh();
}

void DocumentView::TraceNextPaint(
    const std::string& event,
    std::chrono::steady_clock::time_point start) {
    if (PerformanceTraceEnabled()) {
        impl_->pendingPaintTraces.push_back(
            Impl::PendingPaintTrace{event, start, false});
    }
}

void DocumentView::TraceNextPaintProcessAge(const std::string& event) {
    if (PerformanceTraceEnabled()) {
        impl_->pendingPaintTraces.push_back(
            Impl::PendingPaintTrace{event, std::nullopt, true});
    }
}

void DocumentView::OnImageLoaded(wxThreadEvent& event) {
    const ImageLoadedPayload payload =
        event.GetPayload<ImageLoadedPayload>();
    impl_->media->HandleImageLoaded(payload);

    if (impl_->document) {
        impl_->pendingScrollY.Request(ScrollY());
        impl_->layoutDirty = true;
        impl_->layout.reset();
        impl_->layoutCache.erase(impl_->document.get());
        Refresh(false);
    }
}

void DocumentView::OnFlowchartLoaded(wxThreadEvent& event) {
    const FlowchartLoadedPayload payload =
        event.GetPayload<FlowchartLoadedPayload>();
    impl_->media->HandleFlowchartLoaded(payload);

    if (impl_->document) {
        impl_->pendingScrollY.Request(ScrollY());
        impl_->layoutDirty = true;
        impl_->layout.reset();
        impl_->layoutCache.erase(impl_->document.get());
        Refresh(false);
    }
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

int DocumentView::RequestedScrollY() const {
    if (const auto pending = impl_->pendingScrollY.Peek()) {
        return *pending;
    }
    return ScrollY();
}

void DocumentView::SelectAll() {
    if (!impl_->document || impl_->document->blocks.empty()) {
        return;
    }
    const std::size_t last = impl_->document->blocks.size() - 1;
    impl_->selectionAnchor = TextSelectionPoint{0, 0};
    impl_->selectionFocus =
        TextSelectionPoint{
            last, BlockPlainText(impl_->document->blocks[last]).size()};
    impl_->selecting = false;
    impl_->selectionMoved = true;
    impl_->pressedLink.clear();
    Refresh(false);
}

bool DocumentView::CopySelectionToClipboard() {
    if (!impl_->document || !impl_->selectionAnchor ||
        !impl_->selectionFocus) {
        return false;
    }
    const std::string text = PlainTextForSelection(
        *impl_->document,
        TextSelectionRange{*impl_->selectionAnchor, *impl_->selectionFocus});
    if (text.empty() || !wxTheClipboard->Open()) {
        return false;
    }
    const wxString wxText = wxString::FromUTF8(text.data(), text.size());
    const bool copied =
        wxTheClipboard->SetData(new wxTextDataObject(wxText));
    wxTheClipboard->Close();
    if (copied) {
        wxTheClipboard->Flush();
    }
    return copied;
}

std::vector<HeadingAnchor> DocumentView::OutlineHeadings() {
    if (!impl_->document) {
        return {};
    }
    if (!impl_->headingAnchorsBuilt) {
        impl_->headingAnchors = BuildHeadingAnchors(*impl_->document);
        impl_->headingAnchorsBuilt = true;
    }
    return impl_->headingAnchors;
}

bool DocumentView::ScrollToAnchor(const wxString& reference) {
    if (!impl_->document || impl_->document->blocks.empty() ||
        !impl_->layout) {
        return false;
    }
    const wxScopedCharBuffer utf8 = reference.ToUTF8();
    const auto anchors = OutlineHeadings();
    const auto block = FindHeadingBlock(
        anchors,
        std::string_view(utf8.data(), utf8.length()));
    if (!block) {
        return false;
    }

    wxClientDC dc(this);
    DoPrepareDC(dc);
    int y = impl_->PageMargin(dc);
    for (std::size_t index = 0; index < *block; ++index) {
        y += impl_->SlotHeight(impl_->layout->slots[index]);
    }
    Scroll(0, std::max(0, y - FromDIP(10)));
    Refresh(false);
    return true;
}

void DocumentView::SetSearchMatches(
    const std::vector<SearchMatch>& matches, int activeIndex) {
    impl_->searchMatches = matches;
    impl_->activeSearchMatch =
        activeIndex >= 0 &&
                static_cast<std::size_t>(activeIndex) < matches.size()
            ? activeIndex
            : -1;

    if (impl_->activeSearchMatch >= 0 && impl_->document &&
        impl_->layout) {
        const SearchMatch& match = impl_->searchMatches[
            static_cast<std::size_t>(impl_->activeSearchMatch)];
        wxClientDC dc(this);
        DoPrepareDC(dc);
        int y = impl_->PageMargin(dc);
        for (std::size_t index = 0; index < match.blockIndex &&
                                    index < impl_->layout->slots.size();
             ++index) {
            y += impl_->SlotHeight(impl_->layout->slots[index]);
        }
        Scroll(0, std::max(0, y - GetClientSize().y / 4));
    }
    Refresh(false);
}

void DocumentView::ClearSearchMatches() {
    impl_->searchMatches.clear();
    impl_->activeSearchMatch = -1;
    Refresh(false);
}

void DocumentView::ApplyTheme(const ThemePalette& palette) {
    impl_->palette = palette;
    SetBackgroundColour(palette.documentBackground);
    Refresh();
}

void DocumentView::OnSize(wxSizeEvent& event) {
    impl_->tableDrag = nullptr;
    if (event.GetSize().x != impl_->layoutWidth) {
        if (!impl_->suppressSizeReadingAnchor) {
            if (const auto anchor = impl_->CaptureReadingAnchor()) {
                impl_->pendingReadingAnchor = anchor;
            }
        }
        impl_->layoutDirty = true;
    }
    Refresh();
    event.Skip();
}

void DocumentView::OnCharHook(wxKeyEvent& event) {
    if (event.ControlDown()) {
        switch (std::toupper(event.GetKeyCode())) {
            case 'A':
                SelectAll();
                return;
            case 'C':
                if (CopySelectionToClipboard()) {
                    return;
                }
                break;
            default:
                break;
        }
    }
    event.Skip();
}

void DocumentView::OnMouseWheel(wxMouseEvent& event) {
    const int wheelDelta =
        event.GetWheelDelta() > 0 ? event.GetWheelDelta() : 120;
    const double notches =
        static_cast<double>(event.GetWheelRotation()) / wheelDelta;

    if (event.ControlDown()) {
        impl_->zoomWheelAccumulator += notches;
        const int steps = static_cast<int>(impl_->zoomWheelAccumulator);
        if (steps == 0) {
            return;
        }
        impl_->zoomWheelAccumulator -= steps;

        const double nextScale = std::clamp(
            impl_->fontScale + steps * kFontZoomStep,
            kMinFontScale, kMaxFontScale);
        if (std::fabs(nextScale - impl_->fontScale) < 0.001) {
            return;
        }

        int x = 0;
        int y = 0;
        GetViewStart(&x, &y);
        impl_->pendingScrollY.Request(y);
        impl_->fontScale = nextScale;
        impl_->fonts.clear();
        impl_->layoutCache.clear();
        impl_->layoutDirty = true;
        impl_->layout.reset();
        impl_->tableDrag = nullptr;
        Refresh(false);
        return;
    }

    int contentX = 0;
    int contentY = 0;
    CalcUnscrolledPosition(event.GetPosition().x, event.GetPosition().y,
                           &contentX, &contentY);
    BlockLayout* table = impl_->TableAt(wxPoint(contentX, contentY));
    const bool overTableScrollbar =
        table != nullptr && table->tableScrollbarHeight > 0 &&
        contentY >= table->y + table->tableRowsHeight;
    const bool horizontal =
        event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL ||
        event.ShiftDown() || overTableScrollbar;

    if (horizontal && table != nullptr &&
        impl_->TableMaxScroll(*table) > 0) {
        const int distance =
            static_cast<int>(std::lround(FromDIP(110) * notches));
        table->tableScrollX =
            std::clamp(table->tableScrollX - distance, 0,
                       impl_->TableMaxScroll(*table));
        Refresh(false);
        return;
    }

    if (event.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL) {
        event.Skip();
        return;
    }

    const int pageHeight = GetClientSize().y;
    const int distance = static_cast<int>(
        std::lround(pageHeight * kMouseWheelPageFraction * notches));

    if (distance != 0) {
        int x = 0;
        int y = 0;
        GetViewStart(&x, &y);
        if (PerformanceTraceEnabled() && !impl_->scrollRequestedAt) {
            impl_->scrollRequestedAt = std::chrono::steady_clock::now();
        }
        Scroll(0, std::max(0, y - distance));
        Refresh(false);
    }
}

void DocumentView::OnLeftDown(wxMouseEvent& event) {
    int contentX = 0;
    int contentY = 0;
    CalcUnscrolledPosition(event.GetPosition().x, event.GetPosition().y,
                           &contentX, &contentY);
    BlockLayout* table = impl_->TableAt(wxPoint(contentX, contentY));
    if (table != nullptr && table->tableScrollbarHeight > 0 &&
        contentY >= table->y + table->tableRowsHeight) {
        impl_->tableDrag = table;
        impl_->SetTableScrollFromX(*table, contentX);
        if (!HasCapture()) {
            CaptureMouse();
        }
        Refresh(false);
        return;
    }

    if (!impl_->document || impl_->document->blocks.empty()) {
        event.Skip();
        return;
    }
    SetFocus();
    wxClientDC dc(this);
    DoPrepareDC(dc);
    const auto hit = impl_->HitTestPosition(
        dc, wxPoint(contentX, contentY));
    if (!hit) {
        event.Skip();
        return;
    }

    impl_->selectionAnchor = *hit;
    impl_->selectionFocus = *hit;
    impl_->selecting = true;
    impl_->selectionMoved = false;
    impl_->pressedLink = impl_->LinkAt(wxPoint(contentX, contentY));
    if (!HasCapture()) {
        CaptureMouse();
    }
    Refresh(false);
}

void DocumentView::OnMouseMove(wxMouseEvent& event) {
    if (impl_->tableDrag != nullptr && event.LeftIsDown()) {
        int contentX = 0;
        int contentY = 0;
        CalcUnscrolledPosition(event.GetPosition().x, event.GetPosition().y,
                               &contentX, &contentY);
        (void)contentY;
        impl_->SetTableScrollFromX(*impl_->tableDrag, contentX);
        Refresh(false);
        return;
    }

    int contentX = 0;
    int contentY = 0;
    CalcUnscrolledPosition(event.GetPosition().x, event.GetPosition().y,
                           &contentX, &contentY);
    if (impl_->selecting && event.LeftIsDown()) {
        const int clientHeight = GetClientSize().y;
        int scrollStep = 0;
        if (event.GetPosition().y < 0) {
            scrollStep = -FromDIP(24);
        } else if (event.GetPosition().y >= clientHeight) {
            scrollStep = FromDIP(24);
        }
        if (scrollStep != 0) {
            Scroll(0, std::max(0, ScrollY() + scrollStep));
            CalcUnscrolledPosition(
                event.GetPosition().x, event.GetPosition().y,
                &contentX, &contentY);
        }
        wxClientDC dc(this);
        DoPrepareDC(dc);
        const auto hit = impl_->HitTestPosition(
            dc, wxPoint(contentX, contentY));
        if (hit &&
            (!impl_->selectionFocus ||
             hit->blockIndex != impl_->selectionFocus->blockIndex ||
             hit->utf8Offset != impl_->selectionFocus->utf8Offset)) {
            impl_->selectionFocus = *hit;
            impl_->selectionMoved = true;
            SetCursor(wxCursor(wxCURSOR_IBEAM));
            Refresh(false);
        }
        return;
    }

    const wxString href = impl_->LinkAt(wxPoint(contentX, contentY));
    if (href.empty()) {
        SetCursor(wxCursor(impl_->document ? wxCURSOR_IBEAM : wxCURSOR_ARROW));
    } else {
        SetCursor(wxCursor(wxCURSOR_HAND));
    }
    event.Skip();
}

void DocumentView::OnLeftUp(wxMouseEvent& event) {
    if (impl_->tableDrag != nullptr) {
        impl_->tableDrag = nullptr;
        if (HasCapture()) {
            ReleaseMouse();
        }
        Refresh(false);
        return;
    }
    if (!impl_->selecting) {
        event.Skip();
        return;
    }

    impl_->selecting = false;
    if (HasCapture()) {
        ReleaseMouse();
    }
    const bool activateLink =
        !impl_->selectionMoved && !impl_->pressedLink.empty();
    const wxString href = impl_->pressedLink;
    impl_->pressedLink.clear();
    if (activateLink) {
        impl_->ClearSelection();
        if (href.StartsWith("#")) {
            ScrollToAnchor(href);
        } else {
            const ExternalLinkResult result = OpenExternalLink(href);
            if (result == ExternalLinkResult::Failed) {
                wxMessageBox(U8("无法调用系统默认应用打开该链接。"),
                             U8("打开链接失败"), wxOK | wxICON_WARNING, this);
            }
        }
        Refresh(false);
    } else if (!impl_->selectionMoved) {
        impl_->ClearSelection();
        Refresh(false);
    }
}

void DocumentView::OnPaint(wxPaintEvent&) {
    const bool traceEnabled = PerformanceTraceEnabled();
    const std::optional<std::chrono::steady_clock::time_point> paintStart =
        traceEnabled
            ? std::optional<std::chrono::steady_clock::time_point>(
                  std::chrono::steady_clock::now())
            : std::nullopt;
    wxAutoBufferedPaintDC dc(this);
    DoPrepareDC(dc);

    int scrollX = 0;
    int scrollY = 0;
    GetViewStart(&scrollX, &scrollY);
    const wxSize client = GetClientSize();

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(impl_->palette.documentBackground));
    dc.DrawRectangle(scrollX, scrollY, client.x, client.y);
    std::vector<StatusIconDrawCommand> iconCommands;

    if (impl_->document == nullptr) {
        SetVirtualSize(client);
        impl_->DrawPlaceholder(dc, scrollX, scrollY, client.x,
                               &iconCommands);
        litemark::detail::DrawStatusIconBatch(dc, iconCommands);
        return;
    }

    std::optional<double> layoutInitMicroseconds;
    if (!impl_->layout || impl_->layoutDirty ||
        impl_->layoutWidth != client.x) {
        std::optional<std::chrono::steady_clock::time_point> initStart;
        if (traceEnabled) {
            initStart = std::chrono::steady_clock::now();
        }
        impl_->InitializeLayout(dc, client.x);
        if (initStart) {
            layoutInitMicroseconds = static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - *initStart)
                    .count());
        }
    }

    const int pageMargin = impl_->PageMargin(dc);
    const int viewBottom = scrollY + client.y;
    const int layoutStart = std::max(0, scrollY - client.y);
    const int layoutEnd = viewBottom + client.y;
    int y = pageMargin;
    double visibleLayoutMicroseconds = 0;
    double drawMicroseconds = 0;

    for (std::size_t i = 0; i < impl_->layout->slots.size(); ++i) {
        const int height = impl_->SlotHeight(impl_->layout->slots[i]);
        if (y + height < layoutStart) {
            y += height;
            continue;
        }
        if (y > layoutEnd) {
            break;
        }

        std::optional<std::chrono::steady_clock::time_point> layoutStartTime;
        if (traceEnabled) {
            layoutStartTime = std::chrono::steady_clock::now();
        }
        const BlockLayout& blockLayout =
            impl_->LayoutFor(dc, i, y, client.x);
        if (layoutStartTime) {
            visibleLayoutMicroseconds += static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - *layoutStartTime)
                    .count());
        }
        if (blockLayout.y + blockLayout.height >= scrollY &&
            blockLayout.y <= viewBottom) {
            std::optional<std::chrono::steady_clock::time_point> drawStart;
            if (traceEnabled) {
                drawStart = std::chrono::steady_clock::now();
            }
            impl_->DrawBlock(dc, blockLayout, &iconCommands);
            if (drawStart) {
                drawMicroseconds += static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - *drawStart)
                        .count());
            }
        }
        y += impl_->SlotHeight(impl_->layout->slots[i]);
    }

    if (!iconCommands.empty()) {
        std::optional<std::chrono::steady_clock::time_point> drawStart;
        if (traceEnabled) {
            drawStart = std::chrono::steady_clock::now();
        }
        litemark::detail::DrawStatusIconBatch(dc, iconCommands);
        if (drawStart) {
            drawMicroseconds += static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - *drawStart)
                    .count());
        }
    }

    const int totalHeight = impl_->TotalHeight() + pageMargin * 2;
    SetVirtualSize(wxSize(client.x, std::max(client.y, totalHeight)));
    if (impl_->pendingReadingAnchor) {
        impl_->pendingScrollY.Request(
            impl_->ReadingAnchorScrollY(*impl_->pendingReadingAnchor));
        impl_->pendingReadingAnchor.reset();
    }
    if (const auto target = impl_->pendingScrollY.Take()) {
        CallAfter([this, target]() {
            Scroll(0, *target);
            impl_->pendingScrollY.MarkApplied();
            impl_->suppressSizeReadingAnchor = false;
        });
    }

    if (paintStart) {
        const auto finished = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                finished - *paintStart);
        std::optional<double> scrollMicroseconds;
        if (impl_->scrollRequestedAt) {
            scrollMicroseconds = static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    finished - *impl_->scrollRequestedAt)
                    .count());
            impl_->scrollRequestedAt.reset();
        }
        TraceEvent("paint", static_cast<double>(elapsed.count()));
        if (scrollMicroseconds) {
            TraceEvent("scroll", *scrollMicroseconds);
        }
        const bool hasPendingPaintTraces =
            !impl_->pendingPaintTraces.empty();
        for (const Impl::PendingPaintTrace& trace :
             impl_->pendingPaintTraces) {
            const double microseconds =
                trace.processAge
                    ? static_cast<double>(ProcessAgeMicroseconds())
                    : static_cast<double>(
                          std::chrono::duration_cast<
                              std::chrono::microseconds>(
                              finished - *trace.start)
                              .count());
            TraceEvent(trace.event.c_str(), microseconds);
        }
        if (hasPendingPaintTraces) {
            // These are fixed-count events per first document paint. They do
            // not emit once per block, so tracing overhead stays bounded as
            // document size grows.
            TraceEvent("layout_init",
                       layoutInitMicroseconds.value_or(0));
            TraceEvent("visible_layout", visibleLayoutMicroseconds);
            TraceEvent("draw", drawMicroseconds);
        }
        impl_->pendingPaintTraces.clear();
    }
}

}  // namespace litemark
