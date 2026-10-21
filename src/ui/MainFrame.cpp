#include "ui/MainFrame.h"

#include "core/parser/MarkdownParser.h"
#include "platform/windows/PerformanceTrace.h"
#include "platform/windows/FileAssociation.h"
#include "ui/DocumentView.h"
#include "ui/TabStrip.h"

#include <wx/dnd.h>
#include <wx/button.h>
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

#include <atomic>
#include <chrono>
#include <exception>
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

MainFrame::MainFrame(ThemeController& theme)
    : wxFrame(nullptr, wxID_ANY, U8("liteMark"), wxDefaultPosition, wxDefaultSize),
      theme_(theme) {
    CreateMenuBar();

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

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(tabStrip_, 0, wxEXPAND);
    sizer->Add(changeBanner_, 0, wxEXPAND);
    sizer->Add(documentView_, 1, wxEXPAND);
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
    UpdateFromTabs();
    Centre();
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
}

void MainFrame::OpenPaths(const wxArrayString& paths) {
    for (const auto& path : paths) {
        OpenPath(path);
    }
}

void MainFrame::CreateMenuBar() {
    auto* fileMenu = new wxMenu();
    fileMenu->Append(wxID_OPEN, U8("打开...\tCtrl+O"));
    fileMenu->Append(ID_RELOAD, U8("重新加载\tCtrl+R"));
    fileMenu->Append(ID_CLOSE_TAB, U8("关闭当前 Tab\tCtrl+W"));
    fileMenu->AppendSeparator();
    fileMenu->Append(ID_REGISTER_FILE_ASSOCIATION, U8("注册文件关联..."));
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, U8("退出"));

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
    wxMenuItem* outline = viewMenu->Append(ID_OUTLINE, U8("目录（P2）"));
    outline->Enable(false);

    auto* menuBar = new wxMenuBar();
    menuBar->Append(fileMenu, U8("文件"));
    menuBar->Append(viewMenu, U8("查看"));
    SetMenuBar(menuBar);
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
    Bind(wxEVT_BUTTON, &MainFrame::OnReload, this, ID_RELOAD);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        tabs_.DismissChangeNotice();
        UpdateChangeBanner();
        Layout();
    }, ID_DISMISS_CHANGE);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &MainFrame::OnSystemColourChanged, this);
    Bind(wxEVT_FSWATCHER, &MainFrame::OnFileChanged, this);
    Bind(wxEVT_TIMER, &MainFrame::OnChangeTimer, this, ID_CHANGE_TIMER);
    Bind(wxEVT_THREAD, &MainFrame::OnParseCompleted, this, ID_PARSE_COMPLETED);
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
            documentView_->Update();
            const auto start = parseStartTimes_.find(result.tabId);
            if (start != parseStartTimes_.end()) {
                documentView_->TraceNextPaint("hot_open", start->second);
                if (!coldStartTraced_) {
                    documentView_->TraceNextPaintProcessAge("cold_start");
                    coldStartTraced_ = true;
                }
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
        current->scrollY = documentView_->ScrollY();
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

void MainFrame::OnSystemColourChanged(wxSysColourChangedEvent& event) {
    if (theme_.mode() == ThemeMode::System) {
        theme_.Refresh();
    }
    event.Skip();
}

}  // namespace litemark
