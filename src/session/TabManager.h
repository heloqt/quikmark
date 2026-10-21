#pragma once

#include <wx/string.h>

#include <cstdint>
#include <vector>

namespace litemark {

struct TabSession {
    std::uint64_t id = 0;
    wxString path;
    wxString title;
    wxString content;
    int scrollY = 0;
};

enum class TabOpenResult {
    Opened,
    Activated,
    Failed
};

class TabManager {
public:
    TabOpenResult OpenFile(const wxString& path, wxString* error = nullptr);
    TabOpenResult ReloadSelected(wxString* error = nullptr);

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

    std::vector<TabSession> tabs_;
    int selectedIndex_ = -1;
    std::uint64_t nextId_ = 1;
};

}  // namespace litemark
