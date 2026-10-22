#include "ui/DocumentView.h"

#include "core/document/DocumentNavigation.h"
#include "core/document/DocumentText.h"
#include "core/flow/Flowchart.h"
#include "core/policy/ResourcePolicy.h"
#include "platform/windows/ImageDownloader.h"
#include "platform/windows/PerformanceTrace.h"
#include "ui/PendingScroll.h"

#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/clipbrd.h>
#include <wx/cursor.h>
#include <wx/dataobj.h>
#include <wx/dc.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/event.h>
#include <wx/filename.h>
#include <wx/font.h>
#include <wx/fontenum.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/mstream.h>
#include <wx/msw/wrapwin.h>
#include <wx/utils.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr double kMouseWheelPageFraction = 0.45;
constexpr double kFontZoomStep = 0.10;
constexpr double kMinFontScale = 0.75;
constexpr double kMaxFontScale = 2.00;
constexpr int kImageLoadedEventId = wxID_HIGHEST + 501;
constexpr int kFlowchartLoadedEventId = wxID_HIGHEST + 502;

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
    const litemark::Block* block = nullptr;
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
    int markerY = 0;
};

struct BlockLayout::FlowchartLayout {
    struct Node {
        const litemark::FlowNode* source = nullptr;
        wxRect rect;
        TextLayout label;
    };

    struct Edge {
        const litemark::FlowEdge* source = nullptr;
        wxPoint from;
        wxPoint to;
        bool arrow = true;
        TextLayout label;
        wxRect labelRect;
    };

    struct Cluster {
        const litemark::FlowCluster* source = nullptr;
        wxRect rect;
        wxString title;
    };

    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Cluster> clusters;
    int width = 0;
    int height = 0;
};

struct ImageLoadedPayload {
    wxString key;
    std::shared_ptr<wxImage> image;
};

struct FlowchartLoadedPayload {
    std::string source;
    litemark::FlowchartParseResult result;
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

int ScaledPointSize(int pointSize, double fontScale) {
    return std::clamp(
        static_cast<int>(std::lround(pointSize * fontScale)), 6, 72);
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

std::size_t Utf8CodeUnitAdvance(const wxString& text, int index) {
    const unsigned long value = text[index].GetValue();
    if (value <= 0x7FUL) {
        return 1;
    }
    if (value <= 0x7FFUL) {
        return 2;
    }
    if (value >= 0xD800UL && value <= 0xDBFFUL) {
        return 4;
    }
    if (value >= 0xDC00UL && value <= 0xDFFFUL) {
        return 0;
    }
    return 3;
}

std::size_t Utf8ByteLength(const wxString& text) {
    std::size_t length = 0;
    for (int index = 0; index < static_cast<int>(text.length()); ++index) {
        length += Utf8CodeUnitAdvance(text, index);
    }
    return length;
}

void BuildTextLayout(wxDC& dc, TextLayout& layout, int maxWidth,
                     const std::vector<litemark::InlineSpan>& spans,
                     const TextStyle& baseStyle, double fontScale,
                     FontMap* fonts) {
    maxWidth = std::max(20, maxWidth);
    auto fontFor = [&](const TextStyle& style) -> wxFont {
        TextStyle scaled = style;
        scaled.pointSize = ScaledPointSize(style.pointSize, fontScale);
        if (fonts == nullptr) {
            return BuildFont(scaled);
        }
        auto result = fonts->try_emplace(scaled.key(), BuildFont(scaled));
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
                             const wxString& href, int width,
                             std::size_t sourceStart,
                             std::size_t sourceBytes) {
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
                last.style.strikethrough == style.strikethrough &&
                last.href == href;
            if (sameStyle) {
                last.text += text;
                last.sourceEnd = sourceStart + sourceBytes;
                last.width += width;
                line.width += width;
            } else {
                Segment segment;
                segment.text = text;
                segment.style = style;
                segment.href = href;
                segment.x = line.width;
                segment.width = width;
                segment.sourceStart = sourceStart;
                segment.sourceEnd = sourceStart + sourceBytes;
                line.segments.push_back(std::move(segment));
                line.width += width;
            }
        } else {
            Segment segment;
            segment.text = text;
            segment.style = style;
            segment.href = href;
            segment.x = 0;
            segment.width = width;
            segment.sourceStart = sourceStart;
            segment.sourceEnd = sourceStart + sourceBytes;
            line.segments.push_back(std::move(segment));
            line.width = width;
        }
        if (line.height < oldHeight) {
            line.height = oldHeight;
        }
        refreshHeight();
    };

    std::size_t sourceCursor = 0;
    for (const auto& sourceSpan : spans) {
        std::size_t spanCursor = sourceCursor;
        sourceCursor += sourceSpan.text.size();
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
        const wxString href = wxString::FromUTF8(sourceSpan.href);

        while (!text.empty()) {
            const int newline = text.Find('\n');
            wxString part = newline == wxNOT_FOUND ? text : text.Left(newline);
            const bool hardBreak = newline != wxNOT_FOUND;
            text = newline == wxNOT_FOUND ? wxString() : text.Mid(newline + 1);

            while (!part.empty()) {
                if (ensureLine().segments.empty() &&
                    (part.StartsWith(" ") || part.StartsWith("\t"))) {
                    part = part.Mid(1);
                    ++spanCursor;
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
                const std::size_t pieceBytes = Utf8ByteLength(piece);
                appendSegment(piece, style, href, pieceWidth, spanCursor,
                              pieceBytes);
                spanCursor += pieceBytes;
                part = part.Mid(fit);
                if (!part.empty()) {
                    newLine();
                }
            }

            if (hardBreak) {
                newLine();
                ++spanCursor;
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
    struct PendingPaintTrace {
        std::string event;
        std::optional<std::chrono::steady_clock::time_point> start;
        bool processAge = false;
    };

    struct ImageTask {
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

    DocumentView* owner = nullptr;
    std::shared_ptr<const MarkdownDocument> document;
    wxString basePath;
    ThemePalette palette;
    std::shared_ptr<ViewLayout> layout;
    bool layoutDirty = true;
    int layoutWidth = 0;
    PendingScrollPosition pendingScrollY;
    double fontScale = 1.0;
    double zoomWheelAccumulator = 0.0;
    FontMap fonts;
    std::map<const MarkdownDocument*, LayoutCacheEntry> layoutCache;
    std::map<wxString, std::shared_ptr<wxImage>> images;
    std::set<wxString> requestedImages;
    std::set<wxString> failedImages;
    std::vector<std::unique_ptr<ImageTask>> imageTasks;
    std::map<std::string, FlowchartGraph> flowcharts;
    std::set<std::string> requestedFlowcharts;
    std::set<std::string> failedFlowcharts;
    std::vector<std::unique_ptr<ImageTask>> flowchartTasks;
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
    bool selecting = false;
    bool selectionMoved = false;
    wxString pressedLink;

    ~Impl() {
        for (auto& task : imageTasks) {
            if (task->cancelled) {
                task->cancelled->store(true, std::memory_order_relaxed);
            }
        }
        for (auto& task : imageTasks) {
            if (task->thread.joinable()) {
                task->thread.join();
            }
        }
        for (auto& task : flowchartTasks) {
            if (task->cancelled) {
                task->cancelled->store(true, std::memory_order_relaxed);
            }
        }
        for (auto& task : flowchartTasks) {
            if (task->thread.joinable()) {
                task->thread.join();
            }
        }
    }

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
        if (referenceKind != ResourceReferenceKind::LocalOrRelative) {
            return resolved;
        }
        if (basePath.empty()) {
            return resolved;
        }

        wxFileName imagePath(text);
        if (imagePath.IsRelative()) {
            const wxFileName documentPath(basePath);
            imagePath.MakeAbsolute(documentPath.GetPath());
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
        auto task = std::make_unique<ImageTask>();
        task->cancelled = std::make_shared<std::atomic_bool>(false);
        const auto cancelled = task->cancelled;
        ImageTask* rawTask = task.get();
        imageTasks.push_back(std::move(task));

        rawTask->thread = std::thread(
            [this, owner = owner, source, cancelled]() {
            auto image = std::make_shared<wxImage>();
            bool loaded = false;
            if (source.remote) {
                std::vector<std::uint8_t> bytes;
                wxString downloadError;
                if (DownloadImageUrl(source.url, &bytes, &downloadError)) {
                    wxMemoryInputStream stream(bytes.data(), bytes.size());
                    loaded = image->LoadFile(stream);
                }
            } else {
                loaded = image->LoadFile(source.localPath);
            }
            if (loaded) {
                const long long pixels =
                    static_cast<long long>(image->GetWidth()) *
                    image->GetHeight();
                loaded = image->IsOk() && pixels > 0 && pixels <= 64000000LL;
            }
            if (cancelled->load(std::memory_order_relaxed)) {
                return;
            }

            auto* event =
                new wxThreadEvent(wxEVT_THREAD, kImageLoadedEventId);
            event->SetPayload(ImageLoadedPayload{
                source.key,
                loaded ? std::move(image) : nullptr,
            });
            wxQueueEvent(owner, event);
        });
    }

    void BuildImageLayout(wxDC& dc, BlockLayout& item, int width,
                          double scale) {
        (void)dc;
        const Block& block = *item.block;
        item.marker = block.listMarker ? BlockMarker(block) : wxString();
        if (block.listMarker) {
            item.markerX =
                item.x - static_cast<int>(std::lround(26 * scale));
            item.markerY = item.y;
        }

        const ResolvedImageSource source =
            ResolveImageSource(block.imagePath);
        if (!source.valid) {
            item.placeholderText =
                U8("图片不可用\n") +
                (block.imageAlt.empty() ? wxString::FromUTF8(block.imagePath)
                                        : wxString::FromUTF8(block.imageAlt));
            item.height = static_cast<int>(std::lround(120 * scale));
            return;
        }

        const auto found = images.find(source.key);
        if (found == images.end() || !found->second || !found->second->IsOk()) {
            RequestImage(source);
            item.placeholderText =
                failedImages.find(source.key) != failedImages.end()
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
        int targetHeight =
            std::max(1, static_cast<int>(std::lround(
                            static_cast<double>(image.GetHeight()) *
                            targetWidth / image.GetWidth())));
        const int maxHeight = static_cast<int>(std::lround(1600 * scale));
        if (targetHeight > maxHeight) {
            targetHeight = maxHeight;
            targetWidth = std::max(
                1, static_cast<int>(std::lround(
                       static_cast<double>(image.GetWidth()) * targetHeight /
                       image.GetHeight())));
        }

        wxImage scaled = image;
        if (scaled.GetWidth() != targetWidth ||
            scaled.GetHeight() != targetHeight) {
            scaled = scaled.Scale(targetWidth, targetHeight,
                                  wxIMAGE_QUALITY_HIGH);
        }
        item.imageBitmap = wxBitmap(scaled);
        item.width = targetWidth;
        item.height = targetHeight;
    }

    void RequestFlowchart(const std::string& source) {
        if (flowcharts.find(source) != flowcharts.end() ||
            failedFlowcharts.find(source) != failedFlowcharts.end() ||
            requestedFlowcharts.find(source) != requestedFlowcharts.end()) {
            return;
        }

        requestedFlowcharts.insert(source);
        auto task = std::make_unique<ImageTask>();
        task->cancelled = std::make_shared<std::atomic_bool>(false);
        const auto cancelled = task->cancelled;
        ImageTask* rawTask = task.get();
        flowchartTasks.push_back(std::move(task));

        rawTask->thread = std::thread(
            [owner = owner, source, cancelled]() {
                FlowchartParseResult result =
                    ParseMermaidFlowchart(source);
                if (cancelled->load(std::memory_order_relaxed)) {
                    return;
                }
                auto* event = new wxThreadEvent(
                    wxEVT_THREAD, kFlowchartLoadedEventId);
                event->SetPayload(FlowchartLoadedPayload{
                    source,
                    std::move(result),
                });
                wxQueueEvent(owner, event);
            });
    }

    void BuildFlowchartLayout(wxDC& dc, BlockLayout& item, int width,
                              double scale) {
        const Block& block = *item.block;
        if (block.flowSource.empty()) {
            BuildFlowchartFallback(dc, item, width, scale);
            return;
        }

        const auto failed = failedFlowcharts.find(block.flowSource);
        if (failed != failedFlowcharts.end()) {
            BuildFlowchartFallback(dc, item, width, scale);
            return;
        }

        const auto found = flowcharts.find(block.flowSource);
        if (found == flowcharts.end()) {
            RequestFlowchart(block.flowSource);
            item.placeholderText = U8("正在解析 Mermaid flowchart…");
            item.height = static_cast<int>(std::lround(150 * scale));
            return;
        }

        BuildFlowchartGraphLayout(dc, item, found->second, width, scale);
    }

    void BuildFlowchartFallback(wxDC& dc, BlockLayout& item, int width,
                                double scale) {
        item.flowchartFallback = true;
        TextStyle style;
        style.pointSize = 10;
        style.mono = true;
        style.code = true;
        std::vector<InlineSpan> raw(1);
        raw[0].text = item.block->flowSource.empty()
                         ? item.block->raw
                         : item.block->flowSource;
        const int padding = static_cast<int>(std::lround(10 * scale));
        BuildTextLayout(dc, item.text, width - padding * 2, raw, style,
                        fontScale, &fonts);
        item.height = item.text.height + padding * 2;
    }

    FlowNode* FindFlowNode(FlowchartGraph& graph,
                           const std::string& id) const {
        for (auto& node : graph.nodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    }

    const FlowNode* FindFlowNode(const FlowchartGraph& graph,
                                 const std::string& id) const {
        for (const auto& node : graph.nodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    }

    wxPoint BoundaryPoint(const wxRect& rect, const wxPoint& toward) const {
        const double dx = toward.x - (rect.x + rect.width / 2.0);
        const double dy = toward.y - (rect.y + rect.height / 2.0);
        if (std::fabs(dx) < 0.01 && std::fabs(dy) < 0.01) {
            return wxPoint(rect.x + rect.width / 2, rect.y + rect.height / 2);
        }
        const double sx =
            std::fabs(dx) > 0.01 ? std::fabs((rect.width / 2.0) / dx)
                                 : 1e9;
        const double sy =
            std::fabs(dy) > 0.01 ? std::fabs((rect.height / 2.0) / dy)
                                 : 1e9;
        const double factor = std::min(sx, sy);
        return wxPoint(
            static_cast<int>(std::lround(rect.x + rect.width / 2.0 +
                                         dx * factor)),
            static_cast<int>(std::lround(rect.y + rect.height / 2.0 +
                                         dy * factor)));
    }

    void BuildFlowchartGraphLayout(wxDC& dc, BlockLayout& item,
                                   const FlowchartGraph& graph, int width,
                                   double scale) {
        if (graph.nodes.empty()) {
            BuildFlowchartFallback(dc, item, width, scale);
            return;
        }

        auto layout = std::make_unique<BlockLayout::FlowchartLayout>();
        const int margin = static_cast<int>(std::lround(16 * scale));
        const int labelMaxWidth = static_cast<int>(std::lround(190 * scale));
        const int nodePaddingX = static_cast<int>(std::lround(18 * scale));
        const int nodePaddingY = static_cast<int>(std::lround(10 * scale));
        const int minNodeWidth = static_cast<int>(std::lround(84 * scale));
        const int minNodeHeight = static_cast<int>(std::lround(38 * scale));
        const int nodeGap = static_cast<int>(std::lround(26 * scale));
        const int rankGap = static_cast<int>(std::lround(58 * scale));

        layout->nodes.reserve(graph.nodes.size());
        for (const auto& node : graph.nodes) {
            BlockLayout::FlowchartLayout::Node nodeLayout;
            nodeLayout.source = &node;
            TextStyle style;
            style.pointSize = 10;
            std::vector<InlineSpan> label(1);
            label[0].text = node.label;
            BuildTextLayout(dc, nodeLayout.label, labelMaxWidth, label,
                            style, fontScale, &fonts);
            int textWidth = 0;
            for (const auto& line : nodeLayout.label.lines) {
                textWidth = std::max(textWidth, line.width);
            }
            int nodeWidth =
                std::max(minNodeWidth, textWidth + nodePaddingX * 2);
            int nodeHeight =
                std::max(minNodeHeight, nodeLayout.label.height + nodePaddingY * 2);
            if (node.shape == FlowNodeShape::Diamond) {
                nodeWidth = static_cast<int>(std::lround(nodeWidth * 1.25));
                nodeHeight = static_cast<int>(std::lround(nodeHeight * 1.35));
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
        std::vector<int> edgeSources(graph.edges.size(), -1);
        for (std::size_t e = 0; e < graph.edges.size(); ++e) {
            const auto from = index.find(graph.edges[e].from);
            const auto to = index.find(graph.edges[e].to);
            if (from == index.end() || to == index.end()) {
                BuildFlowchartFallback(dc, item, width, scale);
                return;
            }
            edgeSources[e] = from->second;
            edgeTargets[e] = to->second;
            outgoing[from->second].push_back(static_cast<int>(e));
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
        for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
            if (color[i] == 0) {
                dfs(static_cast<int>(i));
            }
        }

        std::vector<int> indegree(graph.nodes.size(), 0);
        for (std::size_t e = 0; e < graph.edges.size(); ++e) {
            if (!ignored[e]) {
                ++indegree[edgeTargets[e]];
            }
        }
        std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
        for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
            if (indegree[i] == 0) {
                ready.push(static_cast<int>(i));
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
                layers[target] = std::max(layers[target], layers[node] + 1);
                if (--indegree[target] == 0) {
                    ready.push(target);
                }
            }
        }

        const int layerCount =
            *std::max_element(layers.begin(), layers.end()) + 1;
        std::vector<std::vector<int>> ranks(layerCount);
        for (std::size_t i = 0; i < layers.size(); ++i) {
            ranks[layers[i]].push_back(static_cast<int>(i));
        }

        const bool vertical =
            graph.direction == FlowDirection::TopDown ||
            graph.direction == FlowDirection::BottomUp;
        if (vertical) {
            std::vector<int> layerHeight(layerCount, minNodeHeight);
            std::vector<int> layerWidth(layerCount, 0);
            for (int layer = 0; layer < layerCount; ++layer) {
                for (std::size_t pos = 0; pos < ranks[layer].size(); ++pos) {
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
            int canvasWidth =
                *std::max_element(layerWidth.begin(), layerWidth.end());
            int y = margin;
            for (int layer = 0; layer < layerCount; ++layer) {
                int x = margin + (canvasWidth - layerWidth[layer]) / 2;
                for (int nodeIndex : ranks[layer]) {
                    auto& rect = layout->nodes[nodeIndex].rect;
                    rect.x = x;
                    rect.y = y + (layerHeight[layer] - rect.height) / 2;
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
                for (std::size_t pos = 0; pos < ranks[layer].size(); ++pos) {
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
            int canvasHeight =
                *std::max_element(layerHeight.begin(), layerHeight.end());
            int x = margin;
            for (int layer = 0; layer < layerCount; ++layer) {
                int y = margin + (canvasHeight - layerHeight[layer]) / 2;
                for (int nodeIndex : ranks[layer]) {
                    auto& rect = layout->nodes[nodeIndex].rect;
                    rect.x = x + (layerWidth[layer] - rect.width) / 2;
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
                const auto foundNode = index.find(nodeId);
                if (foundNode == index.end()) {
                    continue;
                }
                const wxRect& rect = layout->nodes[foundNode->second].rect;
                bounds = first ? rect : bounds.Union(rect);
                first = false;
            }
            if (!first) {
                const int pad = static_cast<int>(std::lround(14 * scale));
                bounds.x -= pad;
                bounds.y -= pad + static_cast<int>(std::lround(20 * scale));
                bounds.width += pad * 2;
                bounds.height += pad * 2 +
                                 static_cast<int>(std::lround(20 * scale));
                BlockLayout::FlowchartLayout::Cluster clusterLayout;
                clusterLayout.source = &cluster;
                clusterLayout.rect = bounds;
                clusterLayout.title = wxString::FromUTF8(cluster.title);
                layout->clusters.push_back(std::move(clusterLayout));
                layout->width =
                    std::max(layout->width, bounds.GetRight() + margin);
                layout->height =
                    std::max(layout->height, bounds.GetBottom() + margin);
            }
        }

        for (const auto& edge : graph.edges) {
            const auto fromIt = index.find(edge.from);
            const auto toIt = index.find(edge.to);
            if (fromIt == index.end() || toIt == index.end()) {
                continue;
            }
            const wxRect& fromRect = layout->nodes[fromIt->second].rect;
            const wxRect& toRect = layout->nodes[toIt->second].rect;
            const wxPoint fromCenter(fromRect.x + fromRect.width / 2,
                                     fromRect.y + fromRect.height / 2);
            const wxPoint toCenter(toRect.x + toRect.width / 2,
                                   toRect.y + toRect.height / 2);
            BlockLayout::FlowchartLayout::Edge edgeLayout;
            edgeLayout.source = &edge;
            edgeLayout.from = BoundaryPoint(fromRect, toCenter);
            edgeLayout.to = BoundaryPoint(toRect, fromCenter);
            edgeLayout.arrow = edge.style != FlowEdgeStyle::NoArrow;
            if (!edge.label.empty()) {
                TextStyle labelStyle;
                labelStyle.pointSize = 9;
                std::vector<InlineSpan> label(1);
                label[0].text = edge.label;
                BuildTextLayout(dc, edgeLayout.label, FromDIPValue(180), label,
                                labelStyle, fontScale, &fonts);
                const int labelX =
                    (edgeLayout.from.x + edgeLayout.to.x) / 2 -
                    edgeLayout.label.lines.front().width / 2;
                const int labelY =
                    (edgeLayout.from.y + edgeLayout.to.y) / 2 -
                    edgeLayout.label.height / 2;
                edgeLayout.labelRect = wxRect(
                    labelX - FromDIPValue(4), labelY - FromDIPValue(2),
                    edgeLayout.label.lines.front().width + FromDIPValue(8),
                    edgeLayout.label.height + FromDIPValue(4));
            }
            layout->edges.push_back(std::move(edgeLayout));
        }

        item.flowchart = std::move(layout);
        item.width = std::min(width, item.flowchart->width);
        item.height = item.flowchart->height;
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
        item.blockIndex = index;
        item.x = x;
        item.y = y + before;
        item.width = width;

        if (block.kind == BlockKind::Rule) {
            item.height = static_cast<int>(std::lround(18 * scale));
        } else if (block.kind == BlockKind::Image) {
            BuildImageLayout(dc, item, width, scale);
        } else if (block.kind == BlockKind::Flowchart) {
            BuildFlowchartLayout(dc, item, width, scale);
        } else if (block.kind == BlockKind::Table) {
            BuildTableLayout(dc, item, width, scale);
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
            const int markerWidth =
                block.listMarker
                    ? static_cast<int>(std::lround(27 * scale))
                    : 0;
            BuildTextLayout(dc, item.text, width - markerWidth, block.spans,
                            style, fontScale, &fonts);
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

    int MeasureCellWidth(wxDC& dc,
                         const std::vector<litemark::InlineSpan>& spans,
                         bool header) {
        int widest = 0;
        for (const auto& sourceSpan : spans) {
            TextStyle style;
            style.pointSize = 10;
            style.bold = header || sourceSpan.bold;
            style.italic = sourceSpan.italic;
            style.mono = sourceSpan.code;
            wxString text = wxString::FromUTF8(sourceSpan.text);
            if (sourceSpan.image) {
                text = text.empty() ? U8("[图片]")
                                    : U8("[图片: ") + text + U8("]");
            }
            dc.SetFont(FontFor(style));
            const wxArrayString lines = wxSplit(text, '\n', '\0');
            for (const auto& line : lines) {
                widest = std::max(widest, dc.GetTextExtent(line).x);
            }
        }
        return widest;
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
            static_cast<int>(std::lround(9 * scale));
        const int minColumnWidth =
            static_cast<int>(std::lround(64 * scale));
        const int maxMinColumnWidth =
            static_cast<int>(std::lround(150 * scale));
        const int desiredColumnWidth =
            static_cast<int>(std::lround(380 * scale));

        std::vector<int> minimums(columnCount, minColumnWidth);
        std::vector<int> desired(columnCount, minColumnWidth);
        for (std::size_t c = 0; c < columnCount; ++c) {
            int contentWidth = 0;
            for (std::size_t r = 0; r < rowCount; ++r) {
                if (c < block.tableRows[r].size()) {
                    contentWidth = std::max(
                        contentWidth,
                        MeasureCellWidth(dc, block.tableRows[r][c].spans,
                                         r == 0));
                }
            }
            desired[c] = std::clamp(contentWidth + cellPadding * 2,
                                    minColumnWidth, desiredColumnWidth);
            minimums[c] = std::clamp(desired[c], minColumnWidth,
                                     maxMinColumnWidth);
        }

        const int totalMinimum =
            std::accumulate(minimums.begin(), minimums.end(), 0);
        const int totalDesired =
            std::accumulate(desired.begin(), desired.end(), 0);
        std::vector<int> columnWidths(columnCount, minColumnWidth);

        if (totalDesired <= width) {
            columnWidths = desired;
        } else if (totalMinimum <= width) {
            int flexible = 0;
            for (std::size_t c = 0; c < columnCount; ++c) {
                flexible += desired[c] - minimums[c];
            }
            const int extra = width - totalMinimum;
            int assigned = 0;
            for (std::size_t c = 0; c < columnCount; ++c) {
                const int weight = desired[c] - minimums[c];
                const int share =
                    flexible > 0 ? extra * weight / flexible : 0;
                columnWidths[c] = minimums[c] + share;
                assigned += columnWidths[c];
            }
            if (!columnWidths.empty() && assigned < width) {
                columnWidths.back() += width - assigned;
            }
        } else {
            columnWidths = minimums;
        }

        item.tableContentWidth =
            std::accumulate(columnWidths.begin(), columnWidths.end(), 0);
        item.tableScrollX =
            std::clamp(item.tableScrollX, 0,
                       std::max(0, item.tableContentWidth - width));
        item.tableScrollbarHeight =
            item.tableContentWidth > width
                ? static_cast<int>(std::lround(9 * scale))
                : 0;

        int y = 0;
        std::vector<int> columnX(columnCount, 0);
        for (std::size_t c = 1; c < columnCount; ++c) {
            columnX[c] = columnX[c - 1] + columnWidths[c - 1];
        }

        for (std::size_t r = 0; r < rowCount; ++r) {
            RowLayout rowLayout;
            rowLayout.y = y;
            for (std::size_t c = 0; c < columnCount; ++c) {
                CellLayout cell;
                cell.x = columnX[c];
                cell.width = columnWidths[c];
                TextStyle style;
                style.pointSize = 10;
                style.bold = r == 0;
                if (c < block.tableRows[r].size()) {
                    BuildTextLayout(dc, cell.text,
                                    columnWidths[c] - cellPadding * 2,
                                    block.tableRows[r][c].spans, style,
                                    fontScale, &fonts);
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
            y += rowHeight;
            item.tableRows.push_back(std::move(rowLayout));
        }
        item.tableRowsHeight = y;
        item.height = y + item.tableScrollbarHeight;
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
                        fontScale, &fonts);

        TextLayout body;
        std::vector<InlineSpan> bodySpans(1);
        bodySpans[0].text = messageBody.ToUTF8().data();
        BuildTextLayout(dc, body, clientWidth - 80, bodySpans, bodyStyle,
                        fontScale, &fonts);

        const int x = scrollX + 40;
        int y = scrollY + 36;
        DrawTextLayout(dc, title, x, y, false);
        y += title.height + 18;
        DrawTextLayout(dc, body, x, y, false);
    }

    void DrawTextLayout(wxDC& dc, const TextLayout& text, int baseX,
                        int baseY, bool blockCode,
                        const std::optional<std::pair<std::size_t,
                                                      std::size_t>>&
                            selection = std::nullopt) {
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
                if (selection &&
                    segment.sourceEnd > segment.sourceStart) {
                    const std::size_t begin =
                        std::max(selection->first, segment.sourceStart);
                    const std::size_t end =
                        std::min(selection->second, segment.sourceEnd);
                    if (begin < end) {
                        const int x0 =
                            XAtSourceOffset(dc, segment, begin);
                        const int x1 =
                            XAtSourceOffset(dc, segment, end);
                        dc.SetPen(*wxTRANSPARENT_PEN);
                        dc.SetBrush(wxBrush(palette.selection));
                        dc.DrawRectangle(segmentX + x0, lineY - 1,
                                         std::max(1, x1 - x0),
                                         line.height + 2);
                    }
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
        const auto selection = SelectionForBlock(item.blockIndex);

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
                               selection);
                break;
            }
            case BlockKind::Table:
                DrawTable(dc, item);
                break;
            case BlockKind::Image:
                DrawImage(dc, item);
                break;
            case BlockKind::Flowchart:
                DrawFlowchart(dc, item);
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
                DrawTextLayout(dc, item.text, item.x, item.y, false,
                               selection);
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
    }

    void DrawImage(wxDC& dc, const BlockLayout& item) {
        if (item.imageBitmap.IsOk()) {
            dc.DrawBitmap(item.imageBitmap, item.x, item.y, true);
        } else {
            dc.SetPen(wxPen(palette.border, 1));
            dc.SetBrush(wxBrush(palette.codeBackground));
            dc.DrawRoundedRectangle(item.x, item.y, item.width, item.height,
                                    FromDIPValue(6));

            TextStyle style;
            style.pointSize = 10;
            dc.SetFont(FontFor(style));
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
                const int textX =
                    item.x + (item.width - sizes[i].x) / 2;
                dc.DrawText(lines[i], textX, textY);
                textY += sizes[i].y;
            }
        }

        if (!item.marker.empty()) {
            TextStyle markerStyle;
            markerStyle.pointSize = 11;
            markerStyle.bold = item.block->task;
            dc.SetFont(FontFor(markerStyle));
            dc.SetTextForeground(palette.mutedText);
            dc.DrawText(item.marker, item.markerX, item.markerY);
        }
    }

    void DrawFlowchart(wxDC& dc, const BlockLayout& item) {
        if (item.flowchartFallback) {
            dc.SetPen(wxPen(palette.border, 1));
            dc.SetBrush(wxBrush(palette.codeBackground));
            dc.DrawRectangle(item.x, item.y, item.width, item.height);
            const int padding = FromDIPValue(10);
            DrawTextLayout(dc, item.text, item.x + padding,
                           item.y + padding, true);
            return;
        }

        if (!item.flowchart) {
            dc.SetPen(wxPen(palette.border, 1));
            dc.SetBrush(wxBrush(palette.codeBackground));
            dc.DrawRoundedRectangle(item.x, item.y, item.width, item.height,
                                    FromDIPValue(6));
            TextStyle style;
            style.pointSize = 10;
            dc.SetFont(FontFor(style));
            dc.SetTextForeground(palette.mutedText);
            dc.DrawLabel(item.placeholderText,
                         wxRect(item.x + FromDIPValue(10),
                                item.y + FromDIPValue(10),
                                item.width - FromDIPValue(20),
                                item.height - FromDIPValue(20)),
                         wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
            return;
        }

        dc.SetClippingRegion(item.x, item.y, item.width, item.height);
        for (const auto& cluster : item.flowchart->clusters) {
            wxRect rect = cluster.rect;
            rect.x += item.x;
            rect.y += item.y;
            dc.SetPen(wxPen(palette.border, 1, wxPENSTYLE_SHORT_DASH));
            dc.SetBrush(wxBrush(palette.windowBackground));
            dc.DrawRoundedRectangle(rect, FromDIPValue(8));
            dc.SetFont(FontFor(TextStyle{10, true, false, false, false}));
            dc.SetTextForeground(palette.mutedText);
            dc.DrawText(cluster.title, rect.x + FromDIPValue(10),
                        rect.y + FromDIPValue(7));
        }

        dc.SetPen(wxPen(palette.mutedText, 1));
        for (const auto& edge : item.flowchart->edges) {
            const wxPoint from(item.x + edge.from.x, item.y + edge.from.y);
            const wxPoint to(item.x + edge.to.x, item.y + edge.to.y);
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
                    const double arrowLength = FromDIPValue(10);
                    const double arrowHalfWidth = FromDIPValue(4);
                    const wxPoint base(
                        static_cast<int>(std::lround(to.x - ux * arrowLength)),
                        static_cast<int>(std::lround(to.y - uy * arrowLength)));
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
                DrawTextLayout(dc, edge.label, labelRect.x + FromDIPValue(4),
                               labelRect.y + FromDIPValue(2), false);
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
                        wxPoint(rect.x + rect.width, rect.y + rect.height / 2),
                        wxPoint(rect.x + rect.width / 2,
                                rect.y + rect.height),
                        wxPoint(rect.x, rect.y + rect.height / 2),
                    };
                    dc.DrawPolygon(4, points);
                    break;
                }
                case FlowNodeShape::Circle:
                    dc.DrawEllipse(rect);
                    break;
                case FlowNodeShape::Rounded:
                    dc.DrawRoundedRectangle(rect, FromDIPValue(12));
                    break;
                default:
                    dc.DrawRectangle(rect);
                    break;
            }

            int labelWidth = 0;
            for (const auto& line : node.label.lines) {
                labelWidth = std::max(labelWidth, line.width);
            }
            DrawTextLayout(dc, node.label,
                           rect.x + (rect.width - labelWidth) / 2,
                           rect.y + (rect.height - node.label.height) / 2,
                           false);
        }
        dc.DestroyClippingRegion();
    }

    void DrawTable(wxDC& dc, const BlockLayout& item) {
        if (item.tableRows.empty()) {
            return;
        }

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
            if (r == 0) {
                dc.SetBrush(wxBrush(palette.codeBackground));
                dc.DrawRectangle(item.x, item.y + row.y, item.width,
                                 row.height);
            } else if (r % 2 == 0) {
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
                               item.y + row.y + padding, false);
            }
        }

        for (std::size_t r = 0; r < item.tableRows.size(); ++r) {
            const RowLayout& row = item.tableRows[r];
            const int rowBottom = item.y + row.y + row.height;
            if (r == 0) {
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
    return wxScrolledWindow::MSWWindowProc(message, wParam, lParam);
}
#endif

void DocumentView::ShowPlaceholder() {
    impl_->tableDrag = nullptr;
    impl_->ClearSelection();
    impl_->document.reset();
    impl_->headingAnchors.clear();
    impl_->headingAnchorsBuilt = false;
    impl_->basePath.clear();
    impl_->messageTitle = U8("liteMark");
    impl_->messageBody = U8(
        "这是一个只读 Markdown 阅读器。\n"
        "使用“文件 > 打开”或把 Markdown 文件拖入窗口。");
    impl_->layoutDirty = true;
    impl_->pendingScrollY.Request(0);
    Refresh();
}

void DocumentView::ShowMessage(const wxString& title, const wxString& body) {
    impl_->tableDrag = nullptr;
    impl_->ClearSelection();
    impl_->document.reset();
    impl_->headingAnchors.clear();
    impl_->headingAnchorsBuilt = false;
    impl_->basePath.clear();
    impl_->messageTitle = title;
    impl_->messageBody = body;
    impl_->layoutDirty = true;
    impl_->pendingScrollY.Request(0);
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
    impl_->basePath = basePath;
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
    impl_->requestedImages.erase(payload.key);
    if (payload.image && payload.image->IsOk()) {
        impl_->images[payload.key] = payload.image;
        impl_->failedImages.erase(payload.key);
    } else {
        impl_->failedImages.insert(payload.key);
    }

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
    impl_->requestedFlowcharts.erase(payload.source);
    if (payload.result.ok) {
        impl_->flowcharts[payload.source] =
            std::move(payload.result.graph);
        impl_->failedFlowcharts.erase(payload.source);
    } else {
        impl_->failedFlowcharts.insert(payload.source);
    }

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

bool DocumentView::ScrollToAnchor(const wxString& reference) {
    if (!impl_->document || impl_->document->blocks.empty() ||
        !impl_->layout) {
        return false;
    }
    const wxScopedCharBuffer utf8 = reference.ToUTF8();
    if (!impl_->headingAnchorsBuilt) {
        impl_->headingAnchors = BuildHeadingAnchors(*impl_->document);
        impl_->headingAnchorsBuilt = true;
    }
    const auto block = FindHeadingBlock(
        impl_->headingAnchors,
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

void DocumentView::ApplyTheme(const ThemePalette& palette) {
    impl_->palette = palette;
    SetBackgroundColour(palette.documentBackground);
    Refresh();
}

void DocumentView::OnSize(wxSizeEvent& event) {
    impl_->tableDrag = nullptr;
    if (event.GetSize().x != impl_->layoutWidth) {
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
        if (PerformanceTraceEnabled()) {
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
    const std::optional<std::chrono::steady_clock::time_point> paintStart =
        PerformanceTraceEnabled()
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
    if (const auto target = impl_->pendingScrollY.Take()) {
        CallAfter([this, target]() { Scroll(0, *target); });
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
        impl_->pendingPaintTraces.clear();
    }
}

}  // namespace litemark
