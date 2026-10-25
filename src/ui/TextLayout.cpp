#include "ui/TextLayout.h"
#include "ui/FontCatalog.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

int BreakPosition(const wxString& text, int fit) {
    if (fit <= 0) {
        return 0;
    }
    if (fit >= static_cast<int>(text.length())) {
        return static_cast<int>(text.length());
    }

    const int lowerBound = std::max(1, fit / 3);
    for (int index = fit - 1; index >= lowerBound; --index) {
        if (text[index] == ' ' || text[index] == '\t') {
            return index;
        }
    }
    return fit;
}

std::size_t Utf8ByteLength(const wxString& text) {
    std::size_t length = 0;
    for (int index = 0; index < static_cast<int>(text.length()); ++index) {
        length += litemark::detail::Utf8CodeUnitAdvance(text, index);
    }
    return length;
}

}  // namespace

namespace litemark::detail {

int LineHeight(wxDC& dc) {
    return std::max(14, dc.GetCharHeight() + 2);
}

int ScaledPointSize(int pointSize, double fontScale) {
    return std::clamp(
        static_cast<int>(std::lround(pointSize * fontScale)), 6, 72);
}

wxFont BuildFont(const TextStyle& style) {
    wxFont font(wxFontInfo(style.pointSize)
                    .FaceName(style.mono ? MonoFaceName() : UiFaceName()));
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

TextStyle BaseStyle(const Block& block) {
    TextStyle style;
    switch (block.kind) {
        case BlockKind::Heading:
            style.bold = true;
            style.pointSize = std::max(12, 25 - block.headingLevel * 2);
            break;
        case BlockKind::Code:
        case BlockKind::Html:
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

void BuildTextLayout(wxDC& dc, TextLayout& layout, int maxWidth,
                     const std::vector<InlineSpan>& spans,
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
            wxString part =
                newline == wxNOT_FOUND ? text : text.Left(newline);
            const bool hardBreak = newline != wxNOT_FOUND;
            text = newline == wxNOT_FOUND ? wxString()
                                          : text.Mid(newline + 1);

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
                for (std::size_t index = 0; index < partial.size(); ++index) {
                    if (partial[index] <= available) {
                        fit = static_cast<int>(index) + 1;
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

int IntrinsicBefore(const Block& block) {
    if (block.continuesPreviousBlock) {
        return 0;
    }
    if (block.kind == BlockKind::Heading) {
        return block.headingLevel <= 2 ? 16 : 12;
    }
    if (block.kind == BlockKind::Code ||
        block.kind == BlockKind::Table ||
        block.kind == BlockKind::Rule) {
        return 14;
    }
    return 8;
}

int IntrinsicAfter(const Block& block) {
    if (block.continuesNextBlock) {
        return 0;
    }
    if (block.kind == BlockKind::Heading) {
        return 10;
    }
    if (block.kind == BlockKind::Code ||
        block.kind == BlockKind::Table) {
        return 14;
    }
    return 7;
}

}  // namespace litemark::detail
