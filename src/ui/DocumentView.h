#pragma once

#include "core/model/Block.h"
#include "ui/ThemeController.h"

#include <wx/scrolwin.h>

#include <chrono>
#include <memory>
#include <string>

class wxPaintEvent;
class wxSizeEvent;
class wxMouseEvent;
class wxThreadEvent;

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
    void ApplyTheme(const ThemePalette& palette);

private:
    struct Impl;

    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnImageLoaded(wxThreadEvent& event);
    void OnFlowchartLoaded(wxThreadEvent& event);

    std::unique_ptr<Impl> impl_;
};

}  // namespace litemark
