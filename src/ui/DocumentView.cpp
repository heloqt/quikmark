#include "ui/DocumentView.h"

#include "platform/windows/ImageDownloader.h"

#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/cursor.h>
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
#include <wx/utils.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <set>
#include <string>
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
    int tableContentWidth = 0;
    int tableRowsHeight = 0;
    int tableScrollX = 0;
    int tableScrollbarHeight = 0;
    wxBitmap imageBitmap;
    wxBitmap placeholderBitmap;
    wxString placeholderText;
    wxString marker;
    int markerX = 0;
    int markerY = 0;
};

struct ImageLoadedPayload {
    wxString key;
    std::shared_ptr<wxImage> image;
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
                             const wxString& href, int width) {
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
                last.width += width;
                line.width += width;
            } else {
                line.segments.push_back(
                    {text, style, href, line.width, width});
                line.width += width;
            }
        } else {
            line.segments.push_back({text, style, href, 0, width});
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
                appendSegment(piece, style, href, pieceWidth);
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

enum class ExternalLinkResult {
    Opened,
    Unsupported,
    Failed
};

ExternalLinkResult OpenExternalLink(const wxString& href) {
    const int colon = href.Find(':');
    if (colon <= 0) {
        return ExternalLinkResult::Unsupported;
    }

    const wxString scheme = href.Left(colon).Lower();
    bool opened = false;
    if (scheme == "http" || scheme == "https") {
        opened = wxLaunchDefaultBrowser(href);
    } else if (scheme == "mailto") {
        opened = wxLaunchDefaultApplication(href);
    } else {
        return ExternalLinkResult::Unsupported;
    }
    return opened ? ExternalLinkResult::Opened : ExternalLinkResult::Failed;
}

}  // namespace

namespace litemark {

struct DocumentView::Impl {
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
    int pendingScrollY = 0;
    double fontScale = 1.0;
    double zoomWheelAccumulator = 0.0;
    FontMap fonts;
    std::map<const MarkdownDocument*, LayoutCacheEntry> layoutCache;
    std::map<wxString, std::shared_ptr<wxImage>> images;
    std::set<wxString> requestedImages;
    std::set<wxString> failedImages;
    std::vector<std::unique_ptr<ImageTask>> imageTasks;
    wxString messageTitle = U8("liteMark");
    wxString messageBody = U8(
        "这是一个只读 Markdown 阅读器。\n"
        "使用“文件 > 打开”或把 Markdown 文件拖入窗口。");
    BlockLayout* tableDrag = nullptr;

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
        const wxString lower = text.Lower();
        if (lower.StartsWith("http://") || lower.StartsWith("https://")) {
            resolved.key = text;
            resolved.url = text;
            resolved.remote = true;
            resolved.valid = true;
            return resolved;
        }
        if (lower.Contains("://") || lower.StartsWith("data:") ||
            lower.StartsWith("file:") || lower.StartsWith("ftp:")) {
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
            if (extension != "png" && extension != "jpg" &&
                extension != "jpeg" && extension != "bmp" &&
                extension != "gif") {
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
        item.x = x;
        item.y = y + before;
        item.width = width;

        if (block.kind == BlockKind::Rule) {
            item.height = static_cast<int>(std::lround(18 * scale));
        } else if (block.kind == BlockKind::Image) {
            BuildImageLayout(dc, item, width, scale);
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
            case BlockKind::Image:
                DrawImage(dc, item);
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
    Bind(wxEVT_THREAD, &DocumentView::OnImageLoaded, this,
         kImageLoadedEventId);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

DocumentView::~DocumentView() = default;

void DocumentView::ShowPlaceholder() {
    impl_->tableDrag = nullptr;
    impl_->document.reset();
    impl_->basePath.clear();
    impl_->messageTitle = U8("liteMark");
    impl_->messageBody = U8(
        "这是一个只读 Markdown 阅读器。\n"
        "使用“文件 > 打开”或把 Markdown 文件拖入窗口。");
    impl_->layoutDirty = true;
    impl_->pendingScrollY = 0;
    Refresh();
}

void DocumentView::ShowMessage(const wxString& title, const wxString& body) {
    impl_->tableDrag = nullptr;
    impl_->document.reset();
    impl_->basePath.clear();
    impl_->messageTitle = title;
    impl_->messageBody = body;
    impl_->layoutDirty = true;
    impl_->pendingScrollY = 0;
    Refresh();
}

void DocumentView::SetDocument(
    std::shared_ptr<const MarkdownDocument> document,
    const wxString& basePath, int scrollY) {
    impl_->tableDrag = nullptr;
    impl_->document = std::move(document);
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
    impl_->pendingScrollY = std::max(0, scrollY);
    Refresh();
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
        impl_->pendingScrollY = ScrollY();
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
        impl_->pendingScrollY = y;
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

    const wxString href = impl_->LinkAt(wxPoint(contentX, contentY));
    if (!href.empty()) {
        const ExternalLinkResult result = OpenExternalLink(href);
        if (result == ExternalLinkResult::Failed) {
            wxMessageBox(U8("无法调用系统默认应用打开该链接。"),
                         U8("打开链接失败"), wxOK | wxICON_WARNING, this);
        }
        return;
    }
    event.Skip();
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
    const wxString href = impl_->LinkAt(wxPoint(contentX, contentY));
    if (href.empty()) {
        SetCursor(wxCursor(wxCURSOR_ARROW));
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
    event.Skip();
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
