#include "platform/FileAssociation.h"

#include <wx/arrstr.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <string>
#include <vector>

namespace {

wxString FindTool(const wxString& name) {
    wxPathList searchPath;
    searchPath.AddEnvList("PATH");
    return searchPath.FindAbsoluteValidPath(name);
}

bool HasAssociationTools() {
    return !FindTool("xdg-mime").empty() &&
           !FindTool("update-mime-database").empty() &&
           !FindTool("update-desktop-database").empty();
}

wxString DataHome() {
    wxString value;
    if (wxGetEnv("XDG_DATA_HOME", &value) && !value.empty()) {
        return value;
    }
    wxFileName path(wxGetHomeDir(), wxString());
    path.AppendDir(".local");
    path.AppendDir("share");
    return path.GetPath();
}

wxString EscapeDesktopExecArg(wxString value) {
    value.Replace("\\", "\\\\");
    value.Replace("\"", "\\\"");
    value.Replace("`", "\\`");
    value.Replace("$", "\\$");
    return "\"" + value + "\"";
}

bool WriteUtf8File(const wxString& path, const wxString& content,
                   wxString* error) {
    wxFile file;
    if (!file.Open(path, wxFile::write)) {
        if (error != nullptr) {
            *error = "无法写入文件：" + path;
        }
        return false;
    }
    const wxScopedCharBuffer utf8 = content.ToUTF8();
    if (file.Write(utf8.data(), utf8.length()) != utf8.length()) {
        if (error != nullptr) {
            *error = "写入文件失败：" + path;
        }
        return false;
    }
    return true;
}

bool RunTool(const wxArrayString& command, wxString* error) {
    std::vector<std::string> storage;
    std::vector<const char*> arguments;
    storage.reserve(command.size());
    arguments.reserve(command.size() + 1);
    for (const wxString& argument : command) {
        const wxScopedCharBuffer utf8 = argument.ToUTF8();
        storage.emplace_back(utf8.data(), utf8.length());
    }
    for (const std::string& argument : storage) {
        arguments.push_back(argument.c_str());
    }
    arguments.push_back(nullptr);
    if (wxExecute(arguments.data(),
                  wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE) != 0) {
        if (error != nullptr) {
            *error = "执行桌面关联命令失败：" + command[0];
        }
        return false;
    }
    return true;
}

}  // namespace

namespace litemark {

bool IsFileAssociationSupported() {
    return HasAssociationTools();
}

bool RegisterLiteMarkFileAssociations(wxString* error) {
    if (!HasAssociationTools()) {
        if (error != nullptr) {
            *error = "缺少 xdg-mime 或 MIME/桌面数据库更新工具。";
        }
        return false;
    }

    const wxString dataHome = DataHome();
    wxFileName applicationsDir(dataHome, wxString());
    applicationsDir.AppendDir("applications");
    wxFileName mimeDir(dataHome, wxString());
    mimeDir.AppendDir("mime");
    mimeDir.AppendDir("packages");
    if (!applicationsDir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) ||
        !mimeDir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        if (error != nullptr) {
            *error = "无法创建用户级桌面数据目录。";
        }
        return false;
    }

    const wxString executable =
        wxStandardPaths::Get().GetExecutablePath();
    const wxString desktop =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=liteMark\n"
        "Comment=Lightweight native Markdown reader\n"
        "Exec=" +
        EscapeDesktopExecArg(executable) +
        " %F\n"
        "Terminal=false\n"
        "Categories=Office;Viewer;\n"
        "MimeType=text/markdown;\n"
        "StartupNotify=true\n";
    const wxString mimeInfo =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
        "  <mime-type type=\"text/markdown\">\n"
        "    <comment>Markdown document</comment>\n"
        "    <glob pattern=\"*.md\"/>\n"
        "    <glob pattern=\"*.markdown\"/>\n"
        "  </mime-type>\n"
        "</mime-info>\n";

    const wxString desktopPath =
        wxFileName(applicationsDir.GetPath(), "litemark.desktop").GetFullPath();
    const wxString mimePath =
        wxFileName(mimeDir.GetPath(), "litemark.xml").GetFullPath();
    if (!WriteUtf8File(desktopPath, desktop, error) ||
        !WriteUtf8File(mimePath, mimeInfo, error)) {
        return false;
    }

    wxArrayString updateMime;
    updateMime.Add(FindTool("update-mime-database"));
    updateMime.Add(wxFileName(dataHome, wxString())
                       .GetFullPath() +
                   "/mime");
    if (!RunTool(updateMime, error)) {
        return false;
    }

    wxArrayString updateDesktop;
    updateDesktop.Add(FindTool("update-desktop-database"));
    updateDesktop.Add(applicationsDir.GetPath());
    if (!RunTool(updateDesktop, error)) {
        return false;
    }

    wxArrayString association;
    association.Add(FindTool("xdg-mime"));
    association.Add("default");
    association.Add("litemark.desktop");
    association.Add("text/markdown");
    return RunTool(association, error);
}

}  // namespace litemark
