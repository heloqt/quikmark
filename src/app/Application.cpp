#include "app/Application.h"

#include "ui/MainFrame.h"
#include "ui/ThemeController.h"

#include <wx/image.h>
#include <wx/timer.h>

namespace litemark {

Application::Application() = default;
Application::~Application() = default;

bool Application::OnInit() {
    SetAppName("liteMark");
    SetVendorName("liteMark");
    wxInitAllImageHandlers();

    theme_ = std::make_unique<ThemeController>();
    frame_ = new MainFrame(*theme_);
    SetTopWindow(frame_);
    frame_->Show();

    bool smokeTest = false;
    for (int i = 1; i < argc; ++i) {
        const wxString argument = argv[i];
        if (argument == "--smoke-test") {
            smokeTest = true;
        } else if (!argument.StartsWith("--")) {
            frame_->OpenPath(argument);
        }
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
    frame_ = nullptr;
    theme_.reset();
    return 0;
}

}  // namespace litemark
