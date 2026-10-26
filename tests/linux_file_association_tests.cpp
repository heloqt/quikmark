#include "platform/FileAssociation.h"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/utils.h>

#include <unistd.h>

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "linux_file_association_tests: " << message << '\n';
    }
    return condition;
}

bool Contains(const wxString& text, const wxString& expected) {
    return text.Find(expected) != wxNOT_FOUND;
}

}  // namespace

int main() {
    wxInitializer initializer;
    if (!Check(initializer.IsOk(), "wxWidgets initialization failed")) {
        return 1;
    }

    wxString oldDataHome;
    const bool hadDataHome = wxGetEnv("XDG_DATA_HOME", &oldDataHome);
    wxString oldAppImage;
    const bool hadAppImage = wxGetEnv("APPIMAGE", &oldAppImage);
    wxFileName directory(wxFileName::GetTempDir(), wxString());
    directory.AppendDir(
        wxString::Format("litemark-association-%ld",
                         static_cast<long>(getpid())));
    if (!Check(directory.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL),
               "unable to create isolated XDG_DATA_HOME")) {
        return 1;
    }
    const wxString root = directory.GetPath();
    wxSetEnv("XDG_DATA_HOME", root);
    const wxString appImage =
        wxFileName(root, "liteMark test.AppImage").GetFullPath();
    wxSetEnv("APPIMAGE", appImage);

    bool passed = false;
    do {
        if (!Check(litemark::IsFileAssociationSupported(),
                   "XDG association tools are unavailable")) {
            break;
        }
        wxString error;
        if (!Check(litemark::RegisterLiteMarkFileAssociations(&error),
                   error.ToUTF8().data())) {
            break;
        }

        const wxString desktop =
            wxFileName(root + "/applications", "litemark.desktop")
                .GetFullPath();
        const wxString mime =
            wxFileName(root + "/mime/packages", "litemark.xml")
                .GetFullPath();
        if (!Check(wxFileExists(desktop), "desktop file was not created") ||
            !Check(wxFileExists(mime), "MIME file was not created")) {
            break;
        }

        wxFile desktopFile(desktop, wxFile::read);
        wxFile mimeFile(mime, wxFile::read);
        wxString desktopText;
        wxString mimeText;
        if (!Check(desktopFile.ReadAll(&desktopText),
                   "desktop file could not be read") ||
            !Check(mimeFile.ReadAll(&mimeText),
                   "MIME file could not be read") ||
            !Check(Contains(desktopText, "MimeType=text/markdown;"),
                   "desktop file lacks Markdown MIME type") ||
            !Check(Contains(desktopText,
                            "Exec=\"" + appImage + "\" %F"),
                   "desktop file does not point at the AppImage") ||
            !Check(Contains(mimeText, "*.markdown"),
                   "MIME file lacks Markdown glob")) {
            break;
        }

        wxArrayString output;
        wxArrayString errors;
        const long result = wxExecute(
            "xdg-mime query default text/markdown",
            output, errors, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
        if (!Check(result == 0, "xdg-mime query failed") ||
            !Check(!output.IsEmpty() &&
                       output[0].Find("litemark.desktop") != wxNOT_FOUND,
                   "xdg-mime did not report liteMark as default")) {
            break;
        }
        passed = true;
    } while (false);

    if (hadDataHome) {
        wxSetEnv("XDG_DATA_HOME", oldDataHome);
    } else {
        wxUnsetEnv("XDG_DATA_HOME");
    }
    if (hadAppImage) {
        wxSetEnv("APPIMAGE", oldAppImage);
    } else {
        wxUnsetEnv("APPIMAGE");
    }
    wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE);
    if (!passed) {
        return 1;
    }

    std::cout << "linux_file_association_tests: passed\n";
    return 0;
}
