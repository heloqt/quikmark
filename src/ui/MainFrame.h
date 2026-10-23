#pragma once

#include "session/TabManager.h"
#include "session/WorkspaceState.h"
#include "core/document/DocumentSearch.h"
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
class wxTextCtrl;
class wxThreadEvent;
class wxTimerEvent;

namespace litemark {

class DocumentView;
class OutlineList;
class OutlineToggleButton;
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
        ID_OUTLINE_TOGGLE,
        ID_NEXT_TAB,
        ID_PREVIOUS_TAB,
        ID_RECENT_FIRST,
        ID_RECENT_LAST = ID_RECENT_FIRST + 4,
        ID_OUTLINE_LIST,
        ID_FIND,
        ID_FIND_TEXT,
        ID_FIND_NEXT,
        ID_FIND_PREVIOUS,
        ID_FIND_CLOSE,
        ID_FIND_TIMER,
        ID_SEARCH_COMPLETED,
        ID_HELP_USAGE,
        ID_ABOUT,
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

    struct SearchTask {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    void CreateMenuBar();
    void CreateFindBar();
    void CreateOutlinePanel();
    void SetOutlineState(OutlineState state);
    void LayoutOutlinePanel();
    void UpdateOutline();
    void OnOutline(wxCommandEvent& event);
    void OnOutlineToggle(wxCommandEvent& event);
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
    void OnFind(wxCommandEvent& event);
    void OnFindText(wxCommandEvent& event);
    void OnFindEnter(wxCommandEvent& event);
    void OnFindNext(wxCommandEvent& event);
    void OnFindPrevious(wxCommandEvent& event);
    void OnFindClose(wxCommandEvent& event);
    void OnFindTimer(wxTimerEvent& event);
    void OnSearchCompleted(wxThreadEvent& event);
    void OnHelpUsage(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void ShowFindBar();
    void HideFindBar();
    void StartSearch();
    void CancelSearchTasks();
    void ShowCurrentSearchMatch();

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
    wxPanel* findBar_ = nullptr;
    wxTextCtrl* findText_ = nullptr;
    wxStaticText* findStatus_ = nullptr;
    wxPanel* outlinePanel_ = nullptr;
    wxPanel* outlineContentPanel_ = nullptr;
    wxBoxSizer* contentSizer_ = nullptr;
    wxStaticText* outlineTitle_ = nullptr;
    OutlineToggleButton* outlineToggleButton_ = nullptr;
    OutlineList* outlineList_ = nullptr;
    wxStaticText* outlineEmptyText_ = nullptr;
    OutlineState outlineState_ = OutlineState::Hidden;
    bool updatingOutline_ = false;
    wxMenu* recentMenu_ = nullptr;
    wxTimer findTimer_;
    std::vector<std::unique_ptr<SearchTask>> searchTasks_;
    std::vector<SearchMatch> searchMatches_;
    int searchMatchIndex_ = -1;
    std::uint64_t searchToken_ = 0;
    std::string activeSearchQuery_;
    bool searchResultTruncated_ = false;
    std::vector<std::unique_ptr<ParseTask>> parseTasks_;
    std::map<std::uint64_t, std::chrono::steady_clock::time_point>
        parseStartTimes_;
    bool coldStartTraced_ = false;
};

}  // namespace litemark
