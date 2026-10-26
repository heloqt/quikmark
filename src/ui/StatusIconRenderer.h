#pragma once

#include "ui/StatusIcons.h"

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>

#include <vector>

class wxDC;

namespace litemark::detail {

struct StatusIconDrawCommand {
    StatusIconId id = StatusIconId::Count;
    wxString fallbackText;
    wxFont fallbackFont;
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

bool DrawStatusIconBatch(
    wxDC& dc, const std::vector<StatusIconDrawCommand>& commands);
bool DrawStatusIconAwareText(wxDC& dc, const wxString& text,
                             const wxFont& font, const wxColour& colour,
                             int x, int y, int height);

}  // namespace litemark::detail
