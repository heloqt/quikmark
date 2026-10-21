#pragma once

#include <wx/app.h>

#include <memory>

namespace litemark {

class MainFrame;
class ThemeController;

class Application : public wxApp {
public:
    Application();
    ~Application() override;

    bool OnInit() override;
    int OnExit() override;

private:
    std::unique_ptr<ThemeController> theme_;
    MainFrame* frame_ = nullptr;
};

}  // namespace litemark
