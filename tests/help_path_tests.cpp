#include "ui/HelpPaths.h"

#include <wx/filename.h>

#include <algorithm>
#include <iostream>

namespace {

bool Contains(const std::vector<wxString>& values,
              const wxString& expected) {
    wxFileName file(expected);
    file.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return std::find(values.begin(), values.end(), file.GetFullPath()) !=
           values.end();
}

}  // namespace

int main() {
    const wxString usage = wxString::FromUTF8("使用说明.md");
    const auto candidates = litemark::detail::UsageDocumentCandidates(
        "/usr/bin/quikmark", "/home/user", "zh_CN");
    if (candidates.empty() ||
        !Contains(candidates,
                  "/usr/share/quikmark/" + usage) ||
        !Contains(candidates,
                  "/usr/share/doc/quikmark/" + usage)) {
        std::cerr << "help_path_tests: installed Linux help path missing\n";
        return 1;
    }

    const auto buildCandidates =
        litemark::detail::UsageDocumentCandidates(
            "/project/build/release/bin/quikmark", "/project", "zh_CN");
    if (!Contains(buildCandidates,
                  "/project/doc/" +
                      wxString::FromUTF8("12-使用说明.md"))) {
        std::cerr << "help_path_tests: build-tree help path missing\n";
        return 1;
    }

    const auto englishCandidates =
        litemark::detail::UsageDocumentCandidates(
            "/usr/bin/quikmark", "/home/user", "en");
    if (!Contains(englishCandidates,
                  "/usr/share/quikmark/Usage.md") ||
        !Contains(englishCandidates,
                  "/usr/share/doc/quikmark/Usage.md")) {
        std::cerr << "help_path_tests: English Linux help path missing\n";
        return 1;
    }

    std::cout << "help_path_tests: passed\n";
    return 0;
}
