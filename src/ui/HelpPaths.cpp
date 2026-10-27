#include "ui/HelpPaths.h"

#include <wx/filename.h>

#include <algorithm>

namespace {

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

wxString Normalize(const wxString& path) {
    wxFileName file(path);
    file.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return file.GetFullPath();
}

}  // namespace

namespace litemark::detail {

std::vector<wxString> UsageDocumentCandidates(
    const wxString& executablePath, const wxString& currentDirectory,
    const wxString& localeCode) {
    const wxFileName executable(executablePath);
    const wxString executableDirectory = executable.GetPath();
    const wxString separator(1, wxFileName::GetPathSeparator());
    const bool chinese = localeCode.Lower().StartsWith("zh");
    const wxString installedName =
        chinese ? U8("使用说明.md") : "Usage.md";
    const wxString sourceName =
        chinese ? U8("12-使用说明.md") : "12-Usage.md";
    std::vector<wxString> candidates = {
        Normalize(executableDirectory + separator + installedName),
        Normalize(executableDirectory + separator + ".." + separator +
                  "share" + separator + "quikmark" + separator +
                  installedName),
        Normalize(executableDirectory + separator + ".." + separator +
                  "share" + separator + "doc" + separator + "quikmark" +
                  separator + installedName),
        Normalize(executableDirectory + separator + ".." + separator +
                  ".." + separator + ".." + separator + "doc" +
                  separator + sourceName),
        Normalize(currentDirectory + separator + "doc" + separator +
                  sourceName),
    };
    candidates.erase(
        std::unique(candidates.begin(), candidates.end()),
        candidates.end());
    return candidates;
}

}  // namespace litemark::detail
