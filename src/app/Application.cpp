#include "app/Application.h"

#include "ui/MainFrame.h"
#include "ui/ThemeController.h"

#include <wx/image.h>

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
        CallAfter([this]() {
            if (frame_ != nullptr) {
                frame_->Close(true);
            }
        });
    }
    return true;
}

int Application::OnExit() {
    frame_ = nullptr;
    theme_.reset();
    return 0;
}

}  // namespace litemark
