#pragma once

#include "session/TabManager.h"
#include "ui/ThemeController.h"

#include <wx/frame.h>
#include <wx/string.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

class wxCommandEvent;
class wxSysColourChangedEvent;
class wxThreadEvent;

namespace litemark {

class DocumentView;
class TabStrip;

class MainFrame : public wxFrame {
public:
    explicit MainFrame(ThemeController& theme);
    ~MainFrame() override;

    void OpenPath(const wxString& path);
    void OpenPaths(const wxArrayString& paths);

private:
    enum : int {
        ID_RELOAD = wxID_HIGHEST + 1,
        ID_CLOSE_TAB,
        ID_APPEARANCE_SYSTEM,
        ID_APPEARANCE_LIGHT,
        ID_APPEARANCE_DARK,
        ID_OUTLINE,
        ID_PARSE_COMPLETED
    };

    struct ParseTask {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> cancelled;
        std::uint64_t tabId = 0;
        std::uint64_t generation = 0;
    };

    void CreateMenuBar();
    void BindEvents();
    void ApplyTheme();
    void UpdateAppearanceMenu();
    void UpdateTabStrip();
    void UpdateFromTabs();
    void StartParse(const TabSession& tab);
    void OnParseCompleted(wxThreadEvent& event);
    void SaveCurrentScroll();
    void ActivateTab(int index);
    void CloseCurrentTab();
    void OnOpen(wxCommandEvent& event);
    void OnReload(wxCommandEvent& event);
    void OnCloseTab(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnAppearance(wxCommandEvent& event);
    void OnSystemColourChanged(wxSysColourChangedEvent& event);

    ThemeController& theme_;
    TabManager tabs_;
    TabStrip* tabStrip_ = nullptr;
    DocumentView* documentView_ = nullptr;
    std::vector<std::unique_ptr<ParseTask>> parseTasks_;
};

}  // namespace litemark
