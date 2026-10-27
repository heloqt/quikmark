#pragma once

#include "core/document/DocumentNavigation.h"
#include "core/document/DocumentSearch.h"
#include "core/model/Block.h"
#include "ui/ThemeController.h"

#include <wx/scrolwin.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class wxPaintEvent;
class wxSizeEvent;
class wxMouseEvent;
class wxKeyEvent;
class wxThreadEvent;
class wxContextMenuEvent;

namespace litemark {

class DocumentView : public wxScrolledWindow {
public:
    explicit DocumentView(wxWindow* parent);
    ~DocumentView() override;

    void ShowPlaceholder();
    void ShowMessage(const wxString& title, const wxString& body);
    void SetDocument(std::shared_ptr<const MarkdownDocument> document,
                     const wxString& basePath,
                     int scrollY = 0);
    void TraceNextPaint(
        const std::string& event,
        std::chrono::steady_clock::time_point start);
    void TraceNextPaintProcessAge(const std::string& event);
    void ForgetDocument(
        const std::shared_ptr<const MarkdownDocument>& document);
    int ScrollY() const;
    int RequestedScrollY() const;
    void SelectAll();
    bool CopySelectionToClipboard();
    std::vector<HeadingAnchor> OutlineHeadings();
    bool ScrollToAnchor(const wxString& reference);
    void SetSearchMatches(const std::vector<SearchMatch>& matches,
                          int activeIndex);
    void ClearSearchMatches();
    void ApplyTheme(const ThemePalette& palette);

private:
    struct Impl;

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam,
                            WXLPARAM lParam) override;
#endif
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnCharHook(wxKeyEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnContextMenu(wxContextMenuEvent& event);
    void OnImageLoaded(wxThreadEvent& event);
    void OnFlowchartLoaded(wxThreadEvent& event);
    bool CopyTableToClipboard(std::size_t blockIndex);

    std::unique_ptr<Impl> impl_;
};

}  // namespace litemark
