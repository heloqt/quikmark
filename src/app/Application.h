#pragma once

#include <wx/app.h>

#include <memory>

namespace litemark {

class MainFrame;
class IpcServer;
class SingletonGuard;
class ThemeController;

class Application : public wxApp {
public:
    Application();
    ~Application() override;

    bool OnInit() override;
    int OnExit() override;

private:
    void OnExternalOpen(wxThreadEvent& event);

    std::unique_ptr<SingletonGuard> singleton_;
    std::unique_ptr<IpcServer> ipcServer_;
    std::unique_ptr<ThemeController> theme_;
    MainFrame* frame_ = nullptr;
};

}  // namespace litemark
