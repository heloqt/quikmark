#include "ui/MainFrame.h"

#include "core/parser/MarkdownParser.h"
#include "platform/windows/PerformanceTrace.h"
#include "platform/windows/FileAssociation.h"
#include "ui/DocumentView.h"
#include "ui/OutlineList.h"
#include "ui/TabStrip.h"

#include <wx/dnd.h>
#include <wx/button.h>
#include <wx/display.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/fswatcher.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

struct ParseCompletion {
    std::uint64_t tabId = 0;
    std::uint64_t generation = 0;
    wxString content;
    std::shared_ptr<const litemark::MarkdownDocument> document;
    wxString error;
};

class MainFrameDropTarget : public wxFileDropTarget {
public:
    explicit MainFrameDropTarget(litemark::MainFrame& frame)
        : frame_(frame) {}

    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames) override {
        frame_.OpenPaths(filenames);
        return !filenames.empty();
    }

private:
    litemark::MainFrame& frame_;
};

}  // namespace

namespace litemark {

MainFrame::MainFrame(ThemeController& theme, const wxString& workspacePath)
    : wxFrame(nullptr, wxID_ANY, U8("liteMark"), wxDefaultPosition, wxDefaultSize),
      theme_(theme),
      workspaceStore_(std::make_unique<WorkspaceStore>(
          std::filesystem::path(workspacePath.ToStdWstring()))) {
    workspace_ = workspaceStore_->Load();
    TraceEvent("startup_workspace",
               static_cast<double>(ProcessAgeMicroseconds()));
    CreateMenuBar();
    TraceEvent("startup_menu",
               static_cast<double>(ProcessAgeMicroseconds()));

    tabStrip_ = new TabStrip(this);
    changeBanner_ = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                FromDIP(wxSize(-1, 40)));
    changeText_ = new wxStaticText(
        changeBanner_, wxID_ANY, U8("文件已在外部修改"),
        wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    auto* reloadButton = new wxButton(
        changeBanner_, ID_RELOAD, U8("重新加载"), wxDefaultPosition,
        wxDefaultSize, wxBU_EXACTFIT | wxBORDER_NONE);
    auto* dismissButton = new wxButton(
        changeBanner_, ID_DISMISS_CHANGE, U8("稍后"), wxDefaultPosition,
        wxDefaultSize, wxBU_EXACTFIT | wxBORDER_NONE);
    auto* bannerSizer = new wxBoxSizer(wxHORIZONTAL);
    bannerSizer->Add(changeText_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(14));
    bannerSizer->Add(reloadButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    bannerSizer->Add(dismissButton, 0,
                     wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
    changeBanner_->SetSizer(bannerSizer);
    changeBanner_->Hide();

    documentView_ = new DocumentView(this);
    CreateOutlinePanel();

    contentSizer_ = new wxBoxSizer(wxHORIZONTAL);
    contentSizer_->Add(outlinePanel_, 0, wxEXPAND);
    contentSizer_->Add(documentView_, 1, wxEXPAND);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(tabStrip_, 0, wxEXPAND);
    sizer->Add(changeBanner_, 0, wxEXPAND);
    sizer->Add(contentSizer_, 1, wxEXPAND);
    SetSizer(sizer);

    SetMinSize(FromDIP(wxSize(720, 480)));
    SetSize(FromDIP(wxSize(1100, 780)));
    SetDropTarget(new MainFrameDropTarget(*this));

    tabStrip_->SetOnSelect([this](int index) { ActivateTab(index); });
    tabStrip_->SetOnClose([this](int index) { CloseTab(index); });
    theme_.SetOnChanged([this]() { ApplyTheme(); });

    fileWatcher_.SetOwner(this);
    changeTimer_.SetOwner(this, ID_CHANGE_TIMER);

    BindEvents();
    ApplyTheme();
    TraceEvent("startup_controls",
               static_cast<double>(ProcessAgeMicroseconds()));
    RestoreWindowPlacement();
    RestoreWorkspace();
    UpdateRecentMenu();
    SetOutlineVisible(workspace_.outlineVisible);
    TraceEvent("startup_restored",
               static_cast<double>(ProcessAgeMicroseconds()));
}

MainFrame::~MainFrame() {
    for (auto& task : parseTasks_) {
        if (task->cancelled) {
            task->cancelled->store(true);
        }
    }
    for (auto& task : parseTasks_) {
        if (task->thread.joinable()) {
            task->thread.join();
        }
    }
}

void MainFrame::OpenPath(const wxString& path) {
    SaveCurrentScroll();
    const auto result = tabs_.OpenFile(path);
    UpdateFromTabs();
    if (result == TabOpenResult::Opened) {
        if (const TabSession* current = tabs_.selected()) {
            WatchFile(current->path);
            StartParse(*current);
        }
    }
    if (const TabSession* current = tabs_.selected()) {
        RememberRecentFile(current->path);
    }
}

void MainFrame::OpenPaths(const wxArrayString& paths) {
    for (const auto& path : paths) {
        OpenPath(path);
    }
}

void MainFrame::RestoreWorkspace() {
    std::size_t selected = 0;
    for (std::size_t index = 0; index < workspace_.tabs.size(); ++index) {
        const auto& saved = workspace_.tabs[index];
        const wxString path =
            wxString::FromUTF8(saved.path.data(), saved.path.size());
        if (!wxFileExists(path)) {
            continue;
        }
        if (tabs_.OpenFile(path) != TabOpenResult::Opened) {
            continue;
        }
        if (TabSession* current = tabs_.selectedMutable()) {
            current->scrollY = std::max(0, saved.scrollY);
            WatchFile(current->path);
            StartParse(*current);
        }
        if (index <= workspace_.selectedTab) {
            selected = tabs_.tabs().size() - 1;
        }
    }

    if (!tabs_.tabs().empty()) {
        tabs_.Activate(selected);
        UpdateFromTabs();
        documentView_->SetFocus();
    } else {
        UpdateFromTabs();
    }
}

void MainFrame::SaveWorkspace() {
    SaveCurrentScroll();
    WorkspaceState state = workspace_;
    state.hasWindowPlacement = true;
    const wxRect bounds = GetScreenRect();
    state.windowX = bounds.x;
    state.windowY = bounds.y;
    state.windowWidth = bounds.width;
    state.windowHeight = bounds.height;
    state.maximized = IsMaximized();
    state.outlineVisible =
        outlinePanel_ != nullptr && outlinePanel_->IsShown();
    state.selectedTab = static_cast<std::size_t>(
        std::max(0, tabs_.selectedIndex()));
    state.tabs.clear();
    for (const auto& tab : tabs_.tabs()) {
        if (!wxFileExists(tab.path)) {
            continue;
        }
        const wxScopedCharBuffer path = tab.path.ToUTF8();
        state.tabs.push_back(SavedTabState{
            std::string(path.data(), path.length()),
            std::max(0, tab.scrollY),
        });
    }
    workspace_ = std::move(state);
    workspaceStore_->Save(workspace_);
}

void MainFrame::RestoreWindowPlacement() {
    if (!workspace_.hasWindowPlacement || workspace_.windowWidth <= 0 ||
        workspace_.windowHeight <= 0) {
        Centre();
        return;
    }

    const wxRect restored(workspace_.windowX, workspace_.windowY,
                          workspace_.windowWidth, workspace_.windowHeight);
    bool visible = false;
    for (unsigned int index = 0; index < wxDisplay::GetCount(); ++index) {
        if (wxDisplay(index).GetClientArea().Intersects(restored)) {
            visible = true;
            break;
        }
    }
    if (visible) {
        SetSize(restored);
    } else {
        Centre();
    }
    if (workspace_.maximized) {
        Maximize(true);
    }
}

void MainFrame::UpdateRecentMenu() {
    if (recentMenu_ == nullptr) {
        return;
    }
    while (recentMenu_->GetMenuItemCount() > 0) {
        recentMenu_->Destroy(
            recentMenu_->FindItemByPosition(recentMenu_->GetMenuItemCount() - 1));
    }
    if (workspace_.recentFiles.empty()) {
        wxMenuItem* empty =
            recentMenu_->Append(wxID_NONE, U8("暂无最近文档"));
        empty->Enable(false);
        return;
    }
    const std::size_t count = std::min<std::size_t>(
        workspace_.recentFiles.size(), 5);
    for (std::size_t index = 0; index < count; ++index) {
        const std::string& path = workspace_.recentFiles[index];
        recentMenu_->Append(
            ID_RECENT_FIRST + static_cast<int>(index),
            wxString::FromUTF8(path.data(), path.size()));
    }
}

void MainFrame::RememberRecentFile(const wxString& path) {
    const wxScopedCharBuffer utf8 = path.ToUTF8();
    WorkspaceStore::AddRecent(
        &workspace_, std::string(utf8.data(), utf8.length()), 5);
    UpdateRecentMenu();
}

void MainFrame::LayoutOutlinePanel() {
    if (outlinePanel_ == nullptr || contentSizer_ == nullptr ||
        !outlinePanel_->IsShown()) {
        return;
    }
    const wxSize client = GetClientSize();
    const int width = std::max(FromDIP(180), client.x * 24 / 100);
    outlinePanel_->SetMinSize(wxSize(width, -1));
    Layout();
}

void MainFrame::UpdateOutline() {
    if (outlinePanel_ == nullptr || outlineList_ == nullptr ||
        !outlinePanel_->IsShown()) {
        return;
    }
    updatingOutline_ = true;
    const auto headings = documentView_->OutlineHeadings();
    outlineList_->SetItems(headings);
    updatingOutline_ = false;
    outlineList_->Show(!headings.empty());
    outlineEmptyText_->Show(headings.empty());
    outlinePanel_->Layout();
    LayoutOutlinePanel();
}

void MainFrame::OnOutline(wxCommandEvent&) {
    SetOutlineVisible(outlinePanel_ == nullptr || !outlinePanel_->IsShown());
}

void MainFrame::SetOutlineVisible(bool visible) {
    if (outlinePanel_ == nullptr || contentSizer_ == nullptr) {
        return;
    }
    contentSizer_->Show(outlinePanel_, visible, true);
    outlinePanel_->Show(visible);
    if (auto* item = GetMenuBar()->FindItem(ID_OUTLINE)) {
        item->Check(visible);
    }
    if (visible) {
        LayoutOutlinePanel();
        UpdateOutline();
    } else {
        Layout();
    }
}

void MainFrame::OnOutlineItem(wxCommandEvent&) {
    if (updatingOutline_ || outlineList_ == nullptr) {
        return;
    }
    const int selected = outlineList_->GetSelection();
    if (selected == wxNOT_FOUND) {
        return;
    }
    documentView_->ScrollToAnchor(
        U8("#") + outlineList_->SlugAt(static_cast<std::size_t>(selected)));
}

void MainFrame::OnFrameSize(wxSizeEvent& event) {
    event.Skip();
    if (outlinePanel_ != nullptr && outlinePanel_->IsShown()) {
        CallAfter([this]() { LayoutOutlinePanel(); });
    }
}

void MainFrame::CreateMenuBar() {
    auto* fileMenu = new wxMenu();
    fileMenu->Append(wxID_OPEN, U8("打开...\tCtrl+O"));
    recentMenu_ = new wxMenu();
    fileMenu->AppendSubMenu(recentMenu_, U8("最近打开"));
    fileMenu->Append(ID_RELOAD, U8("重新加载\tCtrl+R"));
    fileMenu->Append(ID_CLOSE_TAB, U8("关闭当前 Tab\tCtrl+W"));
    fileMenu->AppendSeparator();
    fileMenu->Append(ID_REGISTER_FILE_ASSOCIATION, U8("注册文件关联..."));
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, U8("退出"));

    auto* editMenu = new wxMenu();
    editMenu->Append(wxID_COPY, U8("复制\tCtrl+C"));
    editMenu->Append(wxID_SELECTALL, U8("全选\tCtrl+A"));

    auto* appearanceMenu = new wxMenu();
    appearanceMenu->AppendRadioItem(ID_APPEARANCE_SYSTEM,
                                     ThemeController::DisplayName(ThemeMode::System));
    appearanceMenu->AppendRadioItem(ID_APPEARANCE_LIGHT,
                                     ThemeController::DisplayName(ThemeMode::Light));
    appearanceMenu->AppendRadioItem(ID_APPEARANCE_DARK,
                                     ThemeController::DisplayName(ThemeMode::Dark));

    auto* viewMenu = new wxMenu();
    viewMenu->AppendSubMenu(appearanceMenu, U8("外观"));
    viewMenu->Append(ID_NEXT_TAB, U8("下一个 Tab\tCtrl+Tab"));
    viewMenu->Append(ID_PREVIOUS_TAB, U8("上一个 Tab\tCtrl+Shift+Tab"));
    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem(ID_OUTLINE, U8("目录"));

    auto* menuBar = new wxMenuBar();
    menuBar->Append(fileMenu, U8("文件"));
    menuBar->Append(editMenu, U8("编辑"));
    menuBar->Append(viewMenu, U8("查看"));
    SetMenuBar(menuBar);
}

void MainFrame::CreateOutlinePanel() {
    outlinePanel_ = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, wxBORDER_SIMPLE);

    auto* title = new wxStaticText(outlinePanel_, wxID_ANY, U8("目录"));
    wxFont titleFont(wxFontInfo(11).FaceName("Microsoft YaHei UI"));
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);

    outlineList_ = new OutlineList(outlinePanel_, ID_OUTLINE_LIST);
    outlineEmptyText_ = new wxStaticText(
        outlinePanel_, wxID_ANY, U8("当前文档没有标题"),
        wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    outlineEmptyText_->Hide();

    auto* outlineSizer = new wxBoxSizer(wxVERTICAL);
    outlineSizer->Add(title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
                      FromDIP(16));
    outlineSizer->Add(outlineList_, 1, wxEXPAND | wxALL, FromDIP(8));
    outlineSizer->Add(outlineEmptyText_, 1,
                      wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    outlinePanel_->SetSizer(outlineSizer);
    outlinePanel_->Hide();
}

void MainFrame::BindEvents() {
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnReload, this, ID_RELOAD);
    Bind(wxEVT_MENU, &MainFrame::OnCloseTab, this, ID_CLOSE_TAB);
    Bind(wxEVT_MENU, &MainFrame::OnNextTab, this, ID_NEXT_TAB);
    Bind(wxEVT_MENU, &MainFrame::OnPreviousTab, this,
         ID_PREVIOUS_TAB);
    Bind(wxEVT_MENU, &MainFrame::OnRegisterFileAssociation, this,
         ID_REGISTER_FILE_ASSOCIATION);
    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAppearance, this, ID_APPEARANCE_SYSTEM,
         ID_APPEARANCE_DARK);
    Bind(wxEVT_MENU, &MainFrame::OnRecentFile, this, ID_RECENT_FIRST,
         ID_RECENT_LAST);
    Bind(wxEVT_MENU, &MainFrame::OnOutline, this, ID_OUTLINE);
    Bind(wxEVT_LISTBOX, &MainFrame::OnOutlineItem, this, ID_OUTLINE_LIST);
    Bind(wxEVT_MENU, &MainFrame::OnCopy, this, wxID_COPY);
    Bind(wxEVT_MENU, &MainFrame::OnSelectAll, this, wxID_SELECTALL);
    Bind(wxEVT_BUTTON, &MainFrame::OnReload, this, ID_RELOAD);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        tabs_.DismissChangeNotice();
        UpdateChangeBanner();
        Layout();
        LayoutOutlinePanel();
    }, ID_DISMISS_CHANGE);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &MainFrame::OnSystemColourChanged, this);
    Bind(wxEVT_FSWATCHER, &MainFrame::OnFileChanged, this);
    Bind(wxEVT_TIMER, &MainFrame::OnChangeTimer, this, ID_CHANGE_TIMER);
    Bind(wxEVT_THREAD, &MainFrame::OnParseCompleted, this, ID_PARSE_COMPLETED);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnCloseWindow, this);
    Bind(wxEVT_CHAR_HOOK, &MainFrame::OnCharHook, this);
    Bind(wxEVT_SIZE, &MainFrame::OnFrameSize, this);
}

void MainFrame::ApplyTheme() {
    const auto& palette = theme_.palette();
    SetBackgroundColour(palette.windowBackground);
    tabStrip_->ApplyTheme(palette);
    documentView_->ApplyTheme(palette);
    if (changeBanner_ != nullptr) {
        changeBanner_->SetBackgroundColour(palette.selection);
        changeText_->SetForegroundColour(palette.text);
        for (wxWindow* child : changeBanner_->GetChildren()) {
            child->SetBackgroundColour(palette.selection);
            child->SetForegroundColour(palette.text);
        }
    }
    if (outlinePanel_ != nullptr) {
        outlinePanel_->SetBackgroundColour(palette.documentBackground);
        outlinePanel_->SetForegroundColour(palette.text);
        outlineList_->ApplyTheme(palette);
        outlineEmptyText_->SetForegroundColour(palette.mutedText);
    }
    UpdateAppearanceMenu();
    Refresh();
}

void MainFrame::UpdateAppearanceMenu() {
    if (GetMenuBar() == nullptr) {
        return;
    }

    const ThemeMode current = theme_.mode();
    if (auto* item = GetMenuBar()->FindItem(ID_APPEARANCE_SYSTEM)) {
        item->Check(current == ThemeMode::System);
    }
    if (auto* item = GetMenuBar()->FindItem(ID_APPEARANCE_LIGHT)) {
        item->Check(current == ThemeMode::Light);
    }
    if (auto* item = GetMenuBar()->FindItem(ID_APPEARANCE_DARK)) {
        item->Check(current == ThemeMode::Dark);
    }
}

void MainFrame::UpdateTabStrip() {
    std::vector<wxString> labels;
    labels.reserve(tabs_.tabs().size());
    for (const auto& tab : tabs_.tabs()) {
        wxString label = tab.title;
        if (tab.loadState == DocumentLoadState::Loading) {
            label += U8(" …");
        } else if (tab.loadState == DocumentLoadState::Failed) {
            label += U8(" !");
        }
        if (tab.externalChanged) {
            label += U8(" ●");
        }
        labels.push_back(label);
    }
    tabStrip_->SetTabs(labels, tabs_.selectedIndex());
}

void MainFrame::UpdateFromTabs() {
    UpdateTabStrip();
    const TabSession* current = tabs_.selected();
    if (current == nullptr) {
        documentView_->ShowPlaceholder();
        SetTitle(U8("liteMark"));
    } else {
        if (current->document) {
            documentView_->SetDocument(current->document, current->path,
                                       current->scrollY);
        } else if (current->loadState == DocumentLoadState::Failed) {
            documentView_->ShowMessage(
                U8("无法显示文档"),
                current->parseError.empty() ? U8("Markdown 解析失败")
                                            : current->parseError);
        } else {
            documentView_->ShowMessage(
                U8("正在解析"),
                current->title + U8("\n\nMarkdown 文档正在后台解析。"));
        }
        SetTitle(current->title + U8(" - liteMark"));
    }
    UpdateChangeBanner();
    Layout();
    UpdateOutline();
}

void MainFrame::UpdateChangeBanner() {
    if (changeBanner_ == nullptr) {
        return;
    }

    const TabSession* current = tabs_.selected();
    const bool show =
        current != nullptr && current->externalChanged &&
        !current->changeNoticeDismissed;
    changeBanner_->Show(show);
    if (show) {
        changeText_->SetLabel(
            current->title + U8(" 已在外部修改，是否重新加载？"));
        changeBanner_->Layout();
    }
}

void MainFrame::StartParse(const TabSession& tab) {
    const wxString path = tab.path;
    const std::uint64_t tabId = tab.id;
    const std::uint64_t generation = tab.generation;
    parseStartTimes_[tab.id] = std::chrono::steady_clock::now();

    auto task = std::make_unique<ParseTask>();
    task->cancelled = std::make_shared<std::atomic_bool>(false);
    task->tabId = tab.id;
    task->generation = tab.generation;
    const auto cancelled = task->cancelled;
    ParseTask* rawTask = task.get();
    parseTasks_.push_back(std::move(task));

    rawTask->thread = std::thread(
        [this, path, cancelled, tabId, generation]() {
            wxString error;
            wxString content;
            std::shared_ptr<const MarkdownDocument> document;
            try {
                TabManager::LoadFileAndParse(path, cancelled.get(), &content,
                                             &document, &error);
            } catch (const std::exception& exception) {
                error = wxString::FromUTF8(exception.what());
            } catch (...) {
                error = U8("未知的 Markdown 解析错误");
            }

            if (cancelled->load(std::memory_order_relaxed)) {
                return;
            }

            auto* event =
                new wxThreadEvent(wxEVT_THREAD, ID_PARSE_COMPLETED);
            event->SetPayload(ParseCompletion{
                tabId,
                generation,
                std::move(content),
                std::move(document),
                std::move(error),
            });
            wxQueueEvent(this, event);
        });
}

void MainFrame::OnParseCompleted(wxThreadEvent& event) {
    const ParseCompletion result = event.GetPayload<ParseCompletion>();
    if (!tabs_.CompleteParse(result.tabId, result.generation,
                             result.content, result.document, result.error)) {
        return;
    }

    const TabSession* current = tabs_.selected();
    if (!result.document && current != nullptr && current->document &&
        !result.error.empty()) {
        wxMessageBox(result.error, U8("Markdown 解析失败"),
                     wxOK | wxICON_WARNING, this);
    }
    if (current != nullptr && current->id == result.tabId) {
        UpdateFromTabs();
        if (result.document) {
            const auto start = parseStartTimes_.find(result.tabId);
            if (start != parseStartTimes_.end()) {
                documentView_->TraceNextPaint("hot_open", start->second);
                if (!coldStartTraced_) {
                    documentView_->TraceNextPaintProcessAge("cold_start");
                    coldStartTraced_ = true;
                }
            }
            documentView_->Update();
            if (start != parseStartTimes_.end()) {
                wxString perfLogPath;
                if (wxGetEnv("LITEMARK_PERF_LOG", &perfLogPath) &&
                    !perfLogPath.empty()) {
                    const wxScopedCharBuffer utf8 = perfLogPath.ToUTF8();
                    std::ofstream log(utf8.data(), std::ios::app);
                    const auto elapsed = std::chrono::duration_cast<
                        std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start->second);
                    log << elapsed.count() / 1000.0 << '\n';
                }
            }
        }
    } else {
        UpdateTabStrip();
    }
    parseStartTimes_.erase(result.tabId);
}

void MainFrame::WatchFile(const wxString& path) {
    wxFileName file(path);
    file.MakeAbsolute();
    file.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG);
    const wxString directory = file.GetPath();
    int& watches = watchedDirectories_[directory];
    if (watches++ == 0) {
        fileWatcher_.Add(
            wxFileName::DirName(directory),
            wxFSW_EVENT_CREATE | wxFSW_EVENT_DELETE | wxFSW_EVENT_RENAME |
                wxFSW_EVENT_MODIFY);
    }
}

void MainFrame::UnwatchFile(const wxString& path) {
    wxFileName file(path);
    file.MakeAbsolute();
    file.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG);
    const wxString directory = file.GetPath();
    const auto found = watchedDirectories_.find(directory);
    if (found == watchedDirectories_.end()) {
        return;
    }
    if (--found->second <= 0) {
        fileWatcher_.Remove(wxFileName::DirName(directory));
        watchedDirectories_.erase(found);
    }
}

void MainFrame::OnFileChanged(wxFileSystemWatcherEvent& event) {
    if ((event.GetChangeType() & (wxFSW_EVENT_ERROR | wxFSW_EVENT_WARNING)) != 0) {
        event.Skip();
        return;
    }

    const wxString path = event.GetPath().GetFullPath();
    const wxString newPath = event.GetNewPath().GetFullPath();
    if (tabs_.MarkExternalChange(path) ||
        (!newPath.empty() && tabs_.MarkExternalChange(newPath))) {
        changeTimer_.StartOnce(350);
        UpdateTabStrip();
    }
    event.Skip();
}

void MainFrame::OnChangeTimer(wxTimerEvent&) {
    UpdateFromTabs();
}

void MainFrame::SaveCurrentScroll() {
    if (TabSession* current = tabs_.selectedMutable()) {
        if (current->document) {
            current->scrollY = documentView_->ScrollY();
        }
    }
}

void MainFrame::ActivateTab(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= tabs_.tabs().size()) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    SaveCurrentScroll();
    tabs_.Activate(static_cast<std::size_t>(index));
    UpdateFromTabs();
    documentView_->TraceNextPaint("tab_switch", start);
    documentView_->Update();
}

void MainFrame::ActivateNextTab() {
    if (tabs_.tabs().size() < 2) {
        return;
    }
    const int next =
        (tabs_.selectedIndex() + 1) % static_cast<int>(tabs_.tabs().size());
    ActivateTab(next);
}

void MainFrame::ActivatePreviousTab() {
    if (tabs_.tabs().size() < 2) {
        return;
    }
    const int count = static_cast<int>(tabs_.tabs().size());
    const int previous = (tabs_.selectedIndex() + count - 1) % count;
    ActivateTab(previous);
}

void MainFrame::OnNextTab(wxCommandEvent&) {
    ActivateNextTab();
}

void MainFrame::OnPreviousTab(wxCommandEvent&) {
    ActivatePreviousTab();
}

void MainFrame::CloseCurrentTab() {
    CloseTab(tabs_.selectedIndex());
}

void MainFrame::CloseTab(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= tabs_.tabs().size()) {
        return;
    }
    SaveCurrentScroll();
    const TabSession* current = &tabs_.tabs()[static_cast<std::size_t>(index)];
    if (current != nullptr) {
        const std::uint64_t closingId = current->id;
        for (auto& task : parseTasks_) {
            if (task->tabId == closingId && task->cancelled) {
                task->cancelled->store(true);
            }
        }
        UnwatchFile(current->path);
        documentView_->ForgetDocument(current->document);
    }
    tabs_.CloseAt(static_cast<std::size_t>(index));
    if (tabs_.tabs().empty()) {
        Close(true);
        return;
    }
    UpdateFromTabs();
}

void MainFrame::OnOpen(wxCommandEvent&) {
    wxFileDialog dialog(
        this, U8("打开 Markdown 文档"), wxString(), wxString(),
        U8("Markdown 文档 (*.md;*.markdown)|*.md;*.markdown|所有文件 (*.*)|*.*"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    wxArrayString paths;
    dialog.GetPaths(paths);
    OpenPaths(paths);
}

void MainFrame::OnReload(wxCommandEvent&) {
    SaveCurrentScroll();
    const TabSession* previous = tabs_.selected();
    if (previous != nullptr) {
        for (auto& task : parseTasks_) {
            if (task->tabId == previous->id && task->cancelled) {
                task->cancelled->store(true);
            }
        }
    }
    wxString error;
    if (tabs_.ReloadSelected(&error) == TabOpenResult::Failed) {
        wxMessageBox(error, U8("重新加载失败"), wxOK | wxICON_ERROR, this);
        return;
    }
    if (const TabSession* current = tabs_.selected()) {
        UnwatchFile(current->path);
        WatchFile(current->path);
        StartParse(*current);
    }
    UpdateFromTabs();
}

void MainFrame::OnCloseTab(wxCommandEvent&) {
    CloseCurrentTab();
}

void MainFrame::OnRegisterFileAssociation(wxCommandEvent&) {
    const int answer = wxMessageBox(
        U8("将 liteMark 注册为 .md 和 .markdown 文件的打开方式？\n"
           "该操作只修改当前 Windows 用户的文件关联。"),
        U8("注册文件关联"), wxYES_NO | wxICON_QUESTION, this);
    if (answer != wxYES) {
        return;
    }

    wxString error;
    if (!RegisterLiteMarkFileAssociations(&error)) {
        wxMessageBox(error, U8("注册失败"), wxOK | wxICON_ERROR, this);
        return;
    }
    wxMessageBox(U8("文件关联注册完成。"), U8("liteMark"),
                 wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnExit(wxCommandEvent&) {
    Close(true);
}

void MainFrame::OnAppearance(wxCommandEvent& event) {
    switch (event.GetId()) {
        case ID_APPEARANCE_SYSTEM:
            theme_.SetMode(ThemeMode::System);
            break;
        case ID_APPEARANCE_LIGHT:
            theme_.SetMode(ThemeMode::Light);
            break;
        case ID_APPEARANCE_DARK:
            theme_.SetMode(ThemeMode::Dark);
            break;
        default:
            break;
    }
}

void MainFrame::OnRecentFile(wxCommandEvent& event) {
    const int index = event.GetId() - ID_RECENT_FIRST;
    if (index < 0 ||
        static_cast<std::size_t>(index) >= workspace_.recentFiles.size()) {
        return;
    }
    const std::string& path = workspace_.recentFiles[
        static_cast<std::size_t>(index)];
    OpenPath(wxString::FromUTF8(path.data(), path.size()));
}

void MainFrame::OnCloseWindow(wxCloseEvent& event) {
    SaveWorkspace();
    event.Skip();
}

void MainFrame::OnCharHook(wxKeyEvent& event) {
    if (event.ControlDown()) {
        if (event.GetKeyCode() == 'A') {
            documentView_->SelectAll();
            return;
        }
        if (event.GetKeyCode() == 'C' &&
            documentView_->CopySelectionToClipboard()) {
            return;
        }
    }
    event.Skip();
}

void MainFrame::OnCopy(wxCommandEvent&) {
    documentView_->CopySelectionToClipboard();
}

void MainFrame::OnSelectAll(wxCommandEvent&) {
    documentView_->SelectAll();
}

void MainFrame::OnSystemColourChanged(wxSysColourChangedEvent& event) {
    if (theme_.mode() == ThemeMode::System) {
        theme_.Refresh();
    }
    event.Skip();
}

}  // namespace litemark
