#pragma once

#include "ui/BlockLayout.h"

#include <wx/dc.h>

namespace litemark::detail {

void BuildTableLayout(
    wxDC& dc,
    const Block& block,
    BlockLayout& item,
    int width,
    double scale,
    double fontScale,
    FontMap* fonts);

}  // namespace litemark::detail
