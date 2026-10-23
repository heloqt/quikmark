#pragma once

#include "core/model/Block.h"

#include <wx/string.h>

#include <atomic>
#include <cstdint>
#include <functional>
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
    std::string content;
    std::shared_ptr<const MarkdownDocument> document;
    DocumentLoadState loadState = DocumentLoadState::Loading;
    wxString parseError;
    bool externalChanged = false;
    bool changeNoticeDismissed = false;
    int scrollY = 0;
};

enum class TabOpenResult {
    Opened,
    Activated,
    Failed
};

struct FileLoadTimings {
    double fileReadMicroseconds = 0;
    double utf8ConvertMicroseconds = 0;
    double prefixMd4cMicroseconds = 0;
    double md4cMicroseconds = 0;
    double normalizeMicroseconds = 0;
};

using PartialDocumentCallback =
    std::function<void(std::shared_ptr<const MarkdownDocument>)>;

class TabManager {
public:
    TabOpenResult OpenFile(const wxString& path);
    TabOpenResult ReloadSelected(wxString* error = nullptr);
    bool CompleteParse(std::uint64_t tabId, std::uint64_t generation,
                       const std::string& content,
                       std::shared_ptr<const MarkdownDocument> document,
                       const wxString& error);
    bool MarkExternalChange(const wxString& path);
    void DismissChangeNotice();

    static bool LoadFileAndParse(
        const wxString& path,
        const std::atomic_bool* cancelled,
        std::string* content,
        std::shared_ptr<const MarkdownDocument>* document,
        wxString* error,
        FileLoadTimings* timings = nullptr,
        const PartialDocumentCallback& onPartial = {});

    void Activate(std::size_t index);
    void ActivateNext();
    void CloseAt(std::size_t index);
    void CloseSelected();

    int selectedIndex() const { return selectedIndex_; }
    const std::vector<TabSession>& tabs() const { return tabs_; }
    const TabSession* selected() const;
    TabSession* selectedMutable();

private:
    static bool ReadUtf8File(
        const wxString& path,
        std::string* text,
        wxString* error,
        FileLoadTimings* timings);
    static wxString NormalizePath(const wxString& path);
    int FindPath(const wxString& normalizedPath) const;
    TabSession* FindById(std::uint64_t tabId, std::uint64_t generation);

    std::vector<TabSession> tabs_;
    int selectedIndex_ = -1;
    std::uint64_t nextId_ = 1;
};

}  // namespace litemark
