#pragma once

#include "core/model/Block.h"
#include "ui/ThemeController.h"

#include <wx/scrolwin.h>

#include <memory>

class wxPaintEvent;
class wxSizeEvent;
class wxMouseEvent;

namespace litemark {

class DocumentView : public wxScrolledWindow {
public:
    explicit DocumentView(wxWindow* parent);
    ~DocumentView() override;

    void ShowPlaceholder();
    void ShowMessage(const wxString& title, const wxString& body);
    void SetDocument(std::shared_ptr<const MarkdownDocument> document,
                     int scrollY = 0);
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

    std::unique_ptr<Impl> impl_;
};

}  // namespace litemark
