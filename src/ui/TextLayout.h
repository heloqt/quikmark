#pragma once

#include "ui/BlockLayout.h"

#include <wx/dc.h>
#include <wx/font.h>
#include <wx/string.h>

#include <cstddef>
#include <vector>

namespace litemark::detail {

int LineHeight(wxDC& dc);
int ScaledPointSize(int pointSize, double fontScale);
wxFont BuildFont(const TextStyle& style);
TextStyle BaseStyle(const Block& block);

void BuildTextLayout(
    wxDC& dc,
    TextLayout& layout,
    int maxWidth,
    const std::vector<InlineSpan>& spans,
    const TextStyle& baseStyle,
    double fontScale,
    FontMap* fonts);

int IntrinsicBefore(const Block& block);
int IntrinsicAfter(const Block& block);

std::size_t Utf8CodeUnitAdvance(const wxString& text, int index);

}  // namespace litemark::detail
