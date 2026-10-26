#pragma once

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>

#include <vector>

class wxDC;

namespace litemark::detail {

struct EmojiDrawCommand {
    wxString text;
    wxFont font;
    wxColour colour;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int clipX = 0;
    int clipY = 0;
    int clipWidth = 0;
    int clipHeight = 0;
};

bool DrawColorEmojiBatch(
    wxDC& dc, const std::vector<EmojiDrawCommand>& commands);
bool DrawEmojiAwareText(wxDC& dc, const wxString& text,
                        const wxFont& font, const wxColour& colour,
                        int x, int y, int height);

}  // namespace litemark::detail
