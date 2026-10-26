#pragma once

#include "litemark/StatusIconsGenerated.h"

#include <wx/string.h>

#include <vector>

namespace litemark::detail {

struct StatusIconRun {
    int begin = 0;
    int end = 0;
    bool icon = false;
    StatusIconId id = StatusIconId::Count;
};

bool ContainsStatusIcon(const wxString& text);
std::vector<StatusIconRun> SplitStatusIcons(const wxString& text);

}  // namespace litemark::detail
