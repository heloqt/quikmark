#pragma once

#include "session/TabManager.h"
#include "ui/ThemeController.h"

#include <wx/frame.h>
#include <wx/string.h>

class wxCommandEvent;
class wxSysColourChangedEvent;

namespace litemark {

class DocumentView;
class TabStrip;

class MainFrame : public wxFrame {
public:
    explicit MainFrame(ThemeController& theme);

    void OpenPath(const wxString& path);
    void OpenPaths(const wxArrayString& paths);

private:
    enum : int {
        ID_RELOAD = wxID_HIGHEST + 1,
        ID_CLOSE_TAB,
        ID_APPEARANCE_SYSTEM,
        ID_APPEARANCE_LIGHT,
        ID_APPEARANCE_DARK,
        ID_OUTLINE
    };

    void CreateMenuBar();
    void BindEvents();
    void ApplyTheme();
    void UpdateAppearanceMenu();
    void UpdateFromTabs();
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
};

}  // namespace litemark
