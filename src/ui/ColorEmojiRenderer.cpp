#include "ui/ColorEmojiRenderer.h"

#include "ui/EmojiText.h"

#include <wx/bitmap.h>
#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/geometry.h>
#include <wx/image.h>

#ifdef __WXMSW__
#include <wx/graphics.h>
#endif

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <tuple>

#ifdef __WXMSW__
namespace {

using EmojiCacheKey =
    std::tuple<wxString, wxString, int, int, int, bool,
               unsigned long, int, int>;
using EmojiCache = std::map<EmojiCacheKey, wxBitmap>;

constexpr std::size_t kMaxCachedEmoji = 256;
constexpr int kSpritePadding = 2;

EmojiCacheKey CacheKeyFor(const litemark::detail::EmojiDrawCommand& command) {
    const wxFont& font = command.font;
    return {
        command.text,
        font.GetFaceName(),
        font.GetPointSize(),
        static_cast<int>(font.GetWeight()),
        static_cast<int>(font.GetStyle()),
        font.GetUnderlined(),
        command.colour.GetRGB(),
        command.width,
        command.height,
    };
}

bool DrawWithGdi(wxDC& dc,
                 const std::vector<litemark::detail::EmojiDrawCommand>&
                     commands) {
    for (const auto& command : commands) {
        wxRect clip(command.clipWidth > 0
                        ? wxRect(command.clipX, command.clipY,
                                 command.clipWidth, command.clipHeight)
                        : wxRect(command.x - kSpritePadding,
                                 command.y - kSpritePadding,
                                 command.width + kSpritePadding * 2,
                                 command.height + kSpritePadding * 2));
        dc.SetClippingRegion(clip);
        dc.SetFont(command.font);
        dc.SetTextForeground(command.colour);
        dc.DrawText(command.text, command.x, command.y);
        dc.DestroyClippingRegion();
    }
    return false;
}

}  // namespace
#endif

namespace litemark::detail {

bool DrawColorEmojiBatch(
    wxDC& dc, const std::vector<EmojiDrawCommand>& commands) {
#if defined(__WXMSW__) && wxUSE_GRAPHICS_DIRECT2D
    if (commands.empty()) {
        return false;
    }
    static EmojiCache cache;
    if (cache.size() > kMaxCachedEmoji) {
        cache.clear();
    }

    std::map<EmojiCacheKey, const EmojiDrawCommand*> missing;
    for (const EmojiDrawCommand& command : commands) {
        const EmojiCacheKey key = CacheKeyFor(command);
        if (cache.find(key) == cache.end()) {
            missing.emplace(key, &command);
        }
    }
    wxGraphicsRenderer* renderer =
        wxGraphicsRenderer::GetDirect2DRenderer();
    if (renderer == nullptr) {
        return DrawWithGdi(dc, commands);
    }

    if (!missing.empty()) {
        int cellWidth = 1;
        int cellHeight = 1;
        for (const auto& entry : missing) {
            cellWidth = std::max(
                cellWidth, entry.second->width + kSpritePadding * 2);
            cellHeight = std::max(
                cellHeight, entry.second->height + kSpritePadding * 2);
        }
        const int columns =
            std::min(8, static_cast<int>(missing.size()));
        const int rows =
            (static_cast<int>(missing.size()) + columns - 1) / columns;
        const int rowHeight = rows * cellHeight;
        wxBitmap atlas(columns * cellWidth, rowHeight * 2, 24);
        wxMemoryDC atlasDc(atlas);
        atlasDc.SetPen(*wxTRANSPARENT_PEN);
        atlasDc.SetBrush(*wxBLACK_BRUSH);
        atlasDc.DrawRectangle(0, 0, columns * cellWidth, rowHeight);
        atlasDc.SetBrush(*wxWHITE_BRUSH);
        atlasDc.DrawRectangle(0, rowHeight, columns * cellWidth,
                              rowHeight);
        {
            std::unique_ptr<wxGraphicsContext> graphics(
                renderer->CreateContextFromUnknownDC(atlasDc));
            if (!graphics) {
                return DrawWithGdi(dc, commands);
            }
            int index = 0;
            for (const auto& entry : missing) {
                const EmojiDrawCommand& command = *entry.second;
                const int column = index % columns;
                const int row = index / columns;
                graphics->ResetClip();
                graphics->Clip(column * cellWidth, row * cellHeight,
                               cellWidth, cellHeight);
                graphics->SetFont(command.font, command.colour);
                graphics->DrawText(
                    command.text,
                    column * cellWidth + kSpritePadding,
                    row * cellHeight + kSpritePadding);
                graphics->ResetClip();
                graphics->Clip(column * cellWidth,
                               rowHeight + row * cellHeight,
                               cellWidth, cellHeight);
                graphics->DrawText(
                    command.text,
                    column * cellWidth + kSpritePadding,
                    rowHeight + row * cellHeight + kSpritePadding);
                ++index;
            }
        }
        atlasDc.SelectObject(wxNullBitmap);
        const wxImage atlasImage = atlas.ConvertToImage();

        int index = 0;
        for (const auto& entry : missing) {
            const EmojiDrawCommand& command = *entry.second;
            const int column = index % columns;
            const int row = index / columns;
            const int width = command.width + kSpritePadding * 2;
            const int height = command.height + kSpritePadding * 2;
            wxImage cell(width, height);
            cell.InitAlpha();
            std::fill(cell.GetAlpha(),
                      cell.GetAlpha() +
                          static_cast<std::size_t>(width) * height,
                      0);
            const int sourceX = column * cellWidth;
            const int sourceY = row * cellHeight;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int darkX = sourceX + x;
                    const int darkY = sourceY + y;
                    const int lightY = rowHeight + darkY;
                    const int darkRed =
                        atlasImage.GetRed(darkX, darkY);
                    const int darkGreen =
                        atlasImage.GetGreen(darkX, darkY);
                    const int darkBlue =
                        atlasImage.GetBlue(darkX, darkY);
                    const int lightRed =
                        atlasImage.GetRed(darkX, lightY);
                    const int lightGreen =
                        atlasImage.GetGreen(darkX, lightY);
                    const int lightBlue =
                        atlasImage.GetBlue(darkX, lightY);
                    const int alpha = std::clamp(
                        255 -
                            ((lightRed - darkRed) +
                             (lightGreen - darkGreen) +
                             (lightBlue - darkBlue)) /
                                3,
                        0, 255);
                    if (alpha == 0) {
                        continue;
                    }
                    cell.SetRGB(
                        x, y,
                        std::clamp(darkRed * 255 / alpha, 0, 255),
                        std::clamp(darkGreen * 255 / alpha, 0, 255),
                        std::clamp(darkBlue * 255 / alpha, 0, 255));
                    cell.SetAlpha(x, y,
                                  static_cast<unsigned char>(alpha));
                }
            }
            cache.emplace(entry.first, wxBitmap(cell, 32));
            ++index;
        }
    }

    for (const EmojiDrawCommand& command : commands) {
        const auto found = cache.find(CacheKeyFor(command));
        wxRect clip(command.clipWidth > 0
                        ? wxRect(command.clipX, command.clipY,
                                 command.clipWidth, command.clipHeight)
                        : wxRect(command.x - kSpritePadding,
                                 command.y - kSpritePadding,
                                 command.width + kSpritePadding * 2,
                                 command.height + kSpritePadding * 2));
        if (found == cache.end()) {
            dc.SetClippingRegion(clip);
            dc.SetFont(command.font);
            dc.SetTextForeground(command.colour);
            dc.DrawText(command.text, command.x, command.y);
            dc.DestroyClippingRegion();
            continue;
        }
        dc.SetClippingRegion(clip);
        dc.DrawBitmap(found->second, command.x - kSpritePadding,
                      command.y - kSpritePadding, true);
        dc.DestroyClippingRegion();
    }
    return true;
#else
    (void)dc;
    (void)commands;
    return false;
#endif
}

bool DrawEmojiAwareText(wxDC& dc, const wxString& text,
                        const wxFont& font, const wxColour& colour,
                        int x, int y, int height) {
#if defined(__WXMSW__) && wxUSE_GRAPHICS_DIRECT2D
    if (!ContainsEmojiCandidate(text)) {
        return false;
    }
    dc.SetFont(font);
    dc.SetTextForeground(colour);
    std::vector<EmojiDrawCommand> commands;
    int clipX = 0;
    int clipY = 0;
    int clipWidth = 0;
    int clipHeight = 0;
    const bool hasClip =
        dc.GetClippingBox(&clipX, &clipY, &clipWidth, &clipHeight);
    int previousWidth = 0;
    for (const EmojiTextRun& run : SplitEmojiText(text)) {
        const wxString runText = text.Mid(run.begin, run.end - run.begin);
        const int endWidth = dc.GetTextExtent(text.Left(run.end)).x;
        if (run.emoji) {
            EmojiDrawCommand command;
            command.text = runText;
            command.font = font;
            command.colour = colour;
            command.x = x + previousWidth;
            command.y = y;
            command.width = std::max(0, endWidth - previousWidth);
            command.height = height;
            if (hasClip) {
                command.clipX = clipX;
                command.clipY = clipY;
                command.clipWidth = clipWidth;
                command.clipHeight = clipHeight;
            }
            commands.push_back(std::move(command));
        } else {
            dc.DrawText(runText, x + previousWidth, y);
        }
        previousWidth = endWidth;
    }
    DrawColorEmojiBatch(dc, commands);
    return true;
#else
    (void)dc;
    (void)text;
    (void)font;
    (void)colour;
    (void)x;
    (void)y;
    (void)height;
    return false;
#endif
}

}  // namespace litemark::detail
