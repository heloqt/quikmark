#include "app/UserData.h"

#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/utils.h>

#include <iostream>

namespace {

bool Write(const wxString& path) {
    wxFile file;
    return file.Create(path) && file.Write("data", 4) == 4;
}

}  // namespace

int main() {
    wxFileName root(wxFileName::GetTempDir(), wxString());
    root.AppendDir(
        wxString::Format("quikmark-user-data-%ld",
                         static_cast<long>(wxGetProcessId())));
    const wxString rootPath = root.GetPath();
    wxFileName legacyDirectory(rootPath, wxString());
    legacyDirectory.AppendDir("liteMark");
    const wxString legacy = legacyDirectory.GetPath();
    wxFileName currentDirectory(rootPath, wxString());
    currentDirectory.AppendDir("quikmark");
    const wxString current = currentDirectory.GetPath();
    if (!wxFileName::Mkdir(legacy, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) ||
        !wxDirExists(legacy)) {
        std::cerr << "user_data_tests: unable to create fixture directory: "
                  << legacy << '\n';
        return 1;
    }
    const wxString workspace = wxFileName(legacy, "workspace.ini").GetFullPath();
    const wxString settings = wxFileName(legacy, "settings.ini").GetFullPath();
    if (!Write(workspace) || !Write(settings)) {
        std::cerr << "user_data_tests: unable to create fixtures: "
                  << workspace << '\n';
        return 1;
    }

    const bool migrated =
        litemark::app::MigrateLegacyUserData(legacy, current);
    const bool copied = wxFileExists(
                            wxFileName(current, "workspace.ini").GetFullPath()) &&
                        wxFileExists(
                            wxFileName(current, "settings.ini").GetFullPath());
    wxFileName::Rmdir(rootPath, wxPATH_RMDIR_RECURSIVE);
    if (!migrated || !copied) {
        std::cerr << "user_data_tests: legacy data was not migrated\n";
        return 1;
    }

    std::cout << "user_data_tests: passed\n";
    return 0;
}
