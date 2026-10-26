#include "ui/StatusIconRenderer.h"

#include <wx/bitmap.h>
#include <wx/dc.h>
#include <wx/geometry.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include <algorithm>
#include <map>
#include <tuple>

namespace litemark::detail {

namespace {

using IconCacheKey = std::tuple<StatusIconId, int>;
using IconCache = std::map<IconCacheKey, wxBitmap>;

constexpr std::size_t kMaxCachedIcons = 512;

wxRect CommandClip(const StatusIconDrawCommand& command) {
    if (command.clipWidth > 0) {
        return wxRect(command.clipX, command.clipY,
                      command.clipWidth, command.clipHeight);
    }
    return wxRect(command.x, command.y, command.width, command.height);
}

const wxBitmap* IconBitmap(StatusIconId id, int targetSize) {
    static IconCache cache;
    if (cache.size() > kMaxCachedIcons) {
        cache.clear();
    }
    const IconCacheKey key{id, targetSize};
    const auto found = cache.find(key);
    if (found != cache.end()) {
        return &found->second;
    }

    StatusIconAsset asset;
    if (!GetStatusIconAsset(id, &asset) || asset.bytes == nullptr ||
        asset.size == 0) {
        return nullptr;
    }
    wxMemoryInputStream stream(asset.bytes, asset.size);
    wxImage image(stream, wxBITMAP_TYPE_PNG);
    if (!image.IsOk()) {
        return nullptr;
    }
    if (image.GetWidth() != targetSize ||
        image.GetHeight() != targetSize) {
        image.Rescale(targetSize, targetSize, wxIMAGE_QUALITY_HIGH);
    }
    return &cache.emplace(key, wxBitmap(image, 32)).first->second;
}

void DrawFallback(wxDC& dc, const StatusIconDrawCommand& command,
                  const wxRect& clip) {
    dc.SetClippingRegion(clip);
    dc.SetFont(command.fallbackFont);
    dc.SetTextForeground(command.colour);
    dc.DrawText(command.fallbackText, command.x, command.y);
    dc.DestroyClippingRegion();
}

}  // namespace

bool DrawStatusIconBatch(
    wxDC& dc, const std::vector<StatusIconDrawCommand>& commands) {
    if (commands.empty()) {
        return false;
    }
    for (const StatusIconDrawCommand& command : commands) {
        const wxRect clip = CommandClip(command);
        const int maxSize = std::max(1, std::min(command.width,
                                                 command.height));
        const int size = std::clamp(maxSize, 1, 128);
        const wxBitmap* bitmap = IconBitmap(command.id, size);
        if (bitmap == nullptr) {
            DrawFallback(dc, command, clip);
            continue;
        }
        const int drawX = command.x + (command.width - size) / 2;
        const int drawY = command.y + (command.height - size) / 2;
        dc.SetClippingRegion(clip);
        dc.DrawBitmap(*bitmap, drawX, drawY, true);
        dc.DestroyClippingRegion();
    }
    return true;
}

bool DrawStatusIconAwareText(wxDC& dc, const wxString& text,
                             const wxFont& font, const wxColour& colour,
                             int x, int y, int height) {
    if (!ContainsStatusIcon(text)) {
        return false;
    }
    dc.SetFont(font);
    dc.SetTextForeground(colour);
    int clipX = 0;
    int clipY = 0;
    int clipWidth = 0;
    int clipHeight = 0;
    const bool hasClip =
        dc.GetClippingBox(&clipX, &clipY, &clipWidth, &clipHeight);
    std::vector<StatusIconDrawCommand> commands;
    int previousWidth = 0;
    for (const StatusIconRun& run : SplitStatusIcons(text)) {
        const wxString runText = text.Mid(run.begin, run.end - run.begin);
        const int endWidth = dc.GetTextExtent(text.Left(run.end)).x;
        if (run.icon) {
            StatusIconDrawCommand command;
            command.id = run.id;
            command.fallbackText = runText;
            command.fallbackFont = font;
            command.colour = colour;
            command.x = x + previousWidth;
            command.y = y;
            command.width = std::max(1, endWidth - previousWidth);
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
    DrawStatusIconBatch(dc, commands);
    return true;
}

}  // namespace litemark::detail
