#pragma once

#include <wx/string.h>

#include <vector>

namespace litemark::detail {

std::vector<wxString> UsageDocumentCandidates(
    const wxString& executablePath, const wxString& currentDirectory,
    const wxString& localeCode);

}  // namespace litemark::detail
