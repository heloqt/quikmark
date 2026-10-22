#pragma once

#include "litemark/Version.h"

#include <wx/app.h>

#include <memory>
#include <string_view>

namespace litemark {

inline constexpr std::string_view kApplicationVersion{
    LITEMARK_VERSION_STRING};

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
