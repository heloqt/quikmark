#pragma once

#include "core/model/Block.h"

#include <wx/string.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace litemark {

enum class DocumentLoadState {
    Loading,
    Ready,
    Failed
};

struct TabSession {
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
    wxString path;
    wxString title;
    wxString content;
    std::shared_ptr<const MarkdownDocument> document;
    DocumentLoadState loadState = DocumentLoadState::Loading;
    wxString parseError;
    int scrollY = 0;
};

enum class TabOpenResult {
    Opened,
    Activated,
    Failed
};

class TabManager {
public:
    TabOpenResult OpenFile(const wxString& path);
    TabOpenResult ReloadSelected(wxString* error = nullptr);
    bool CompleteParse(std::uint64_t tabId, std::uint64_t generation,
                       const wxString& content,
                       std::shared_ptr<const MarkdownDocument> document,
                       const wxString& error);

    static bool LoadFileAndParse(
        const wxString& path,
        const std::atomic_bool* cancelled,
        wxString* content,
        std::shared_ptr<const MarkdownDocument>* document,
        wxString* error);

    void Activate(std::size_t index);
    void ActivateNext();
    void CloseSelected();

    int selectedIndex() const { return selectedIndex_; }
    const std::vector<TabSession>& tabs() const { return tabs_; }
    const TabSession* selected() const;
    TabSession* selectedMutable();

private:
    static bool ReadUtf8File(const wxString& path, wxString* text, wxString* error);
    static wxString NormalizePath(const wxString& path);
    int FindPath(const wxString& normalizedPath) const;
    TabSession* FindById(std::uint64_t tabId, std::uint64_t generation);

    std::vector<TabSession> tabs_;
    int selectedIndex_ = -1;
    std::uint64_t nextId_ = 1;
};

}  // namespace litemark
