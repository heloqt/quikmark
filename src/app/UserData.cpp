#include "app/UserData.h"

#include "app/ProductInfo.h"

#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace {

wxString DirectoryUnderRoot(const wxString& root, const wxString& name) {
    wxFileName directory(root, wxString());
    directory.AppendDir(name);
    return directory.GetPath();
}

}  // namespace

namespace litemark::app {

wxString ConfigRoot() {
    wxString configured;
    if ((wxGetEnv("QUIKMARK_CONFIG_DIR", &configured) ||
         wxGetEnv("LITEMARK_CONFIG_DIR", &configured)) &&
        !configured.empty()) {
        return configured;
    }
    return wxStandardPaths::Get().GetUserConfigDir();
}

wxString ConfigDirectoryName() {
#ifdef __WXMSW__
    return kProductName;
#else
    return kProductId;
#endif
}

wxString ConfigDirectory() {
    return DirectoryUnderRoot(ConfigRoot(), ConfigDirectoryName());
}

wxString ConfigFilePath(const wxString& name) {
    const wxString directory = ConfigDirectory();
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return wxFileName(directory, name).GetFullPath();
}

bool MigrateLegacyUserData(const wxString& legacyDirectory,
                           const wxString& destinationDirectory) {
    if (!wxDirExists(legacyDirectory) ||
        legacyDirectory == destinationDirectory) {
        return false;
    }
    wxFileName::Mkdir(destinationDirectory, wxS_DIR_DEFAULT,
                      wxPATH_MKDIR_FULL);
    bool migrated = false;
    for (const wxString& name : {wxString("workspace.ini"),
                                 wxString("settings.ini")}) {
        const wxString source = wxFileName(legacyDirectory, name).GetFullPath();
        const wxString destination =
            wxFileName(destinationDirectory, name).GetFullPath();
        if (wxFileExists(source) && !wxFileExists(destination) &&
            wxCopyFile(source, destination, false)) {
            migrated = true;
        }
    }
    return migrated;
}

void MigrateLegacyUserData() {
    const wxString root = ConfigRoot();
    MigrateLegacyUserData(
        DirectoryUnderRoot(root, kLegacyProductName),
        DirectoryUnderRoot(root, ConfigDirectoryName()));
}

}  // namespace litemark::app
