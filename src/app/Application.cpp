#include "app/Application.h"

#include "app/ProductInfo.h"
#include "app/UserData.h"
#include "platform/IpcServer.h"
#include "platform/PerformanceTrace.h"
#include "platform/SingletonGuard.h"
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
    SetAppName(kProductName);
    SetVendorName(kProductName);
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
        "QuikMark.SingleInstance.Mutex");
    if (!singleton_->IsPrimary()) {
        if (!SendOpenRequestToPrimary(paths)) {
            wxMessageBox(wxString::FromUTF8(
                             "无法连接正在运行的 QuikMark 实例。"),
                         kProductName, wxOK | wxICON_ERROR);
        }
        CallAfter([this]() { ExitMainLoop(); });
        return true;
    }

    app::MigrateLegacyUserData();
    theme_ = std::make_unique<ThemeController>();
    const wxString workspacePath =
        app::ConfigFilePath("workspace.ini");
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
