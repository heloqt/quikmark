#pragma once

#include "session/TabManager.h"
#include "session/WorkspaceState.h"
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
class wxCloseEvent;
class wxKeyEvent;
class wxBoxSizer;
class wxFileSystemWatcherEvent;
class wxMenu;
class wxPanel;
class wxStaticText;
class wxSysColourChangedEvent;
class wxSizeEvent;
class wxThreadEvent;
class wxTimerEvent;

namespace litemark {

class DocumentView;
class OutlineList;
class TabStrip;

class MainFrame : public wxFrame {
public:
    explicit MainFrame(ThemeController& theme, const wxString& workspacePath);
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
        ID_NEXT_TAB,
        ID_PREVIOUS_TAB,
        ID_RECENT_FIRST,
        ID_RECENT_LAST = ID_RECENT_FIRST + 4,
        ID_OUTLINE_LIST,
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
    void CreateOutlinePanel();
    void SetOutlineVisible(bool visible);
    void LayoutOutlinePanel();
    void UpdateOutline();
    void OnOutline(wxCommandEvent& event);
    void OnOutlineItem(wxCommandEvent& event);
    void OnFrameSize(wxSizeEvent& event);
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
    void RestoreWorkspace();
    void SaveWorkspace();
    void RestoreWindowPlacement();
    void UpdateRecentMenu();
    void RememberRecentFile(const wxString& path);
    void ActivateTab(int index);
    void ActivateNextTab();
    void ActivatePreviousTab();
    void OnNextTab(wxCommandEvent& event);
    void OnPreviousTab(wxCommandEvent& event);
    void CloseTab(int index);
    void CloseCurrentTab();
    void OnOpen(wxCommandEvent& event);
    void OnReload(wxCommandEvent& event);
    void OnCloseTab(wxCommandEvent& event);
    void OnRegisterFileAssociation(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnAppearance(wxCommandEvent& event);
    void OnRecentFile(wxCommandEvent& event);
    void OnCloseWindow(wxCloseEvent& event);
    void OnCharHook(wxKeyEvent& event);
    void OnCopy(wxCommandEvent& event);
    void OnSelectAll(wxCommandEvent& event);
    void OnSystemColourChanged(wxSysColourChangedEvent& event);

    ThemeController& theme_;
    std::unique_ptr<WorkspaceStore> workspaceStore_;
    WorkspaceState workspace_;
    TabManager tabs_;
    wxFileSystemWatcher fileWatcher_;
    wxTimer changeTimer_;
    std::map<wxString, int> watchedDirectories_;
    TabStrip* tabStrip_ = nullptr;
    DocumentView* documentView_ = nullptr;
    wxPanel* changeBanner_ = nullptr;
    wxStaticText* changeText_ = nullptr;
    wxPanel* outlinePanel_ = nullptr;
    wxBoxSizer* contentSizer_ = nullptr;
    OutlineList* outlineList_ = nullptr;
    wxStaticText* outlineEmptyText_ = nullptr;
    bool updatingOutline_ = false;
    wxMenu* recentMenu_ = nullptr;
    std::vector<std::unique_ptr<ParseTask>> parseTasks_;
    std::map<std::uint64_t, std::chrono::steady_clock::time_point>
        parseStartTimes_;
    bool coldStartTraced_ = false;
};

}  // namespace litemark
