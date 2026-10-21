#include "ui/MainFrame.h"

#include "core/parser/MarkdownParser.h"
#include "ui/DocumentView.h"
#include "ui/TabStrip.h"

#include <wx/dnd.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>

#include <atomic>
#include <exception>
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
    documentView_ = new DocumentView(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(tabStrip_, 0, wxEXPAND);
    sizer->Add(documentView_, 1, wxEXPAND);
    SetSizer(sizer);

    SetMinSize(FromDIP(wxSize(720, 480)));
    SetSize(FromDIP(wxSize(1100, 780)));
    SetDropTarget(new MainFrameDropTarget(*this));

    tabStrip_->SetOnSelect([this](int index) { ActivateTab(index); });
    tabStrip_->SetOnClose([this](int index) { CloseTab(index); });
    theme_.SetOnChanged([this]() { ApplyTheme(); });

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
    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAppearance, this, ID_APPEARANCE_SYSTEM,
         ID_APPEARANCE_DARK);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &MainFrame::OnSystemColourChanged, this);
    Bind(wxEVT_THREAD, &MainFrame::OnParseCompleted, this, ID_PARSE_COMPLETED);
}

void MainFrame::ApplyTheme() {
    const auto& palette = theme_.palette();
    SetBackgroundColour(palette.windowBackground);
    tabStrip_->ApplyTheme(palette);
    documentView_->ApplyTheme(palette);
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
            documentView_->SetDocument(current->document, current->scrollY);
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
    Layout();
}

void MainFrame::StartParse(const TabSession& tab) {
    const wxString path = tab.path;
    const std::uint64_t tabId = tab.id;
    const std::uint64_t generation = tab.generation;

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
    } else {
        UpdateTabStrip();
    }
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
    SaveCurrentScroll();
    tabs_.Activate(static_cast<std::size_t>(index));
    UpdateFromTabs();
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
        StartParse(*current);
    }
    UpdateFromTabs();
}

void MainFrame::OnCloseTab(wxCommandEvent&) {
    CloseCurrentTab();
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
