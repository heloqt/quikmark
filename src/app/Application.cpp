#include "app/Application.h"

#include "platform/windows/IpcServer.h"
#include "platform/windows/PerformanceTrace.h"
#include "platform/windows/SingletonGuard.h"
#include "ui/MainFrame.h"
#include "ui/ThemeController.h"

#include <wx/event.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include <vector>

namespace litemark {

Application::Application() = default;
Application::~Application() = default;

bool Application::OnInit() {
    TraceEvent("startup_begin",
               static_cast<double>(ProcessAgeMicroseconds()));
    SetAppName("liteMark");
    SetVendorName("liteMark");
    wxInitAllImageHandlers();
    TraceEvent("startup_images",
               static_cast<double>(ProcessAgeMicroseconds()));

    std::vector<wxString> paths;
    bool smokeTest = false;
    for (int i = 1; i < argc; ++i) {
        const wxString argument = argv[i];
        if (argument == "--smoke-test") {
            smokeTest = true;
        } else if (!argument.StartsWith("--")) {
            paths.push_back(argument);
        }
    }

    singleton_ = std::make_unique<SingletonGuard>(
        L"Local\\liteMark.SingleInstance.Mutex");
    if (!singleton_->IsPrimary()) {
        if (!SendOpenRequestToPrimary(paths)) {
            wxMessageBox(wxString::FromUTF8(
                             "无法连接正在运行的 liteMark 实例。"),
                         "liteMark", wxOK | wxICON_ERROR);
        }
        CallAfter([this]() { ExitMainLoop(); });
        return true;
    }

    theme_ = std::make_unique<ThemeController>();
    wxString configRoot;
    if (!wxGetEnv("LITEMARK_CONFIG_DIR", &configRoot) ||
        configRoot.empty()) {
        configRoot = wxStandardPaths::Get().GetUserConfigDir();
    }
    wxFileName configDirectory(configRoot, wxString());
    configDirectory.AppendDir("liteMark");
    configDirectory.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString workspacePath =
        wxFileName(configDirectory.GetPath(), "workspace.ini").GetFullPath();
    TraceEvent("startup_before_frame",
               static_cast<double>(ProcessAgeMicroseconds()));
    frame_ = new MainFrame(*theme_, workspacePath);
    TraceEvent("startup_frame",
               static_cast<double>(ProcessAgeMicroseconds()));
    SetTopWindow(frame_);
    frame_->Show();
    TraceEvent("startup_shown",
               static_cast<double>(ProcessAgeMicroseconds()));

    Bind(LITEMARK_EVT_OPEN_PATHS, &Application::OnExternalOpen, this);
    ipcServer_ = std::make_unique<IpcServer>(this);

    for (const auto& path : paths) {
        frame_->OpenPath(path);
    }

    if (smokeTest) {
        auto* timer = new wxTimer(frame_);
        frame_->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            if (frame_ != nullptr) {
                frame_->Close(true);
            }
        });
        timer->StartOnce(500);
    }
    return true;
}

int Application::OnExit() {
    ipcServer_.reset();
    frame_ = nullptr;
    theme_.reset();
    singleton_.reset();
    return 0;
}

void Application::OnExternalOpen(wxThreadEvent& event) {
    if (frame_ == nullptr) {
        return;
    }

    const wxArrayString paths = event.GetPayload<wxArrayString>();
    if (frame_->IsIconized()) {
        frame_->Iconize(false);
    }
    frame_->Show();
    frame_->Raise();
    if (!paths.empty()) {
        frame_->OpenPaths(paths);
    }
    frame_->RequestUserAttention(wxUSER_ATTENTION_INFO);
}

}  // namespace litemark
