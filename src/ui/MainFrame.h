#pragma once

#include "session/TabManager.h"
#include "ui/ThemeController.h"

#include <wx/frame.h>
#include <wx/fswatcher.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <thread>
#include <vector>

class wxCommandEvent;
class wxFileSystemWatcherEvent;
class wxPanel;
class wxStaticText;
class wxSysColourChangedEvent;
class wxThreadEvent;
class wxTimerEvent;

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
        ID_REGISTER_FILE_ASSOCIATION,
        ID_APPEARANCE_SYSTEM,
        ID_APPEARANCE_LIGHT,
        ID_APPEARANCE_DARK,
        ID_OUTLINE,
        ID_CHANGE_TIMER,
        ID_DISMISS_CHANGE,
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
    void UpdateChangeBanner();
    void StartParse(const TabSession& tab);
    void WatchFile(const wxString& path);
    void UnwatchFile(const wxString& path);
    void OnParseCompleted(wxThreadEvent& event);
    void OnFileChanged(wxFileSystemWatcherEvent& event);
    void OnChangeTimer(wxTimerEvent& event);
    void SaveCurrentScroll();
    void ActivateTab(int index);
    void CloseTab(int index);
    void CloseCurrentTab();
    void OnOpen(wxCommandEvent& event);
    void OnReload(wxCommandEvent& event);
    void OnCloseTab(wxCommandEvent& event);
    void OnRegisterFileAssociation(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnAppearance(wxCommandEvent& event);
    void OnSystemColourChanged(wxSysColourChangedEvent& event);

    ThemeController& theme_;
    TabManager tabs_;
    wxFileSystemWatcher fileWatcher_;
    wxTimer changeTimer_;
    std::map<wxString, int> watchedDirectories_;
    TabStrip* tabStrip_ = nullptr;
    DocumentView* documentView_ = nullptr;
    wxPanel* changeBanner_ = nullptr;
    wxStaticText* changeText_ = nullptr;
    std::vector<std::unique_ptr<ParseTask>> parseTasks_;
    std::map<std::uint64_t, std::chrono::steady_clock::time_point>
        parseStartTimes_;
};

}  // namespace litemark
