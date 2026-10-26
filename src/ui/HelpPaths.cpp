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
    const wxString& executablePath, const wxString& currentDirectory) {
    const wxFileName executable(executablePath);
    const wxString executableDirectory = executable.GetPath();
    const wxString separator(1, wxFileName::GetPathSeparator());
    std::vector<wxString> candidates = {
        Normalize(executableDirectory + separator + U8("使用说明.md")),
        Normalize(executableDirectory + separator + ".." + separator +
                  "share" + separator + "litemark" + separator +
                  U8("使用说明.md")),
        Normalize(executableDirectory + separator + ".." + separator +
                  "share" + separator + "doc" + separator + "litemark" +
                  separator + U8("使用说明.md")),
        Normalize(executableDirectory + separator + ".." + separator +
                  ".." + separator + ".." + separator + "doc" +
                  separator + U8("12-使用说明.md")),
        Normalize(currentDirectory + separator + "doc" + separator +
                  U8("12-使用说明.md")),
    };
    candidates.erase(
        std::unique(candidates.begin(), candidates.end()),
        candidates.end());
    return candidates;
}

}  // namespace litemark::detail
