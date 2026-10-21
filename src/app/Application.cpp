#include "app/Application.h"

#include "platform/windows/IpcServer.h"
#include "platform/windows/SingletonGuard.h"
#include "ui/MainFrame.h"
#include "ui/ThemeController.h"

#include <wx/event.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/timer.h>

#include <vector>

namespace litemark {

Application::Application() = default;
Application::~Application() = default;

bool Application::OnInit() {
    SetAppName("liteMark");
    SetVendorName("liteMark");
    wxInitAllImageHandlers();

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
    frame_ = new MainFrame(*theme_);
    SetTopWindow(frame_);
    frame_->Show();

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
