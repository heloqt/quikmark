#pragma once

#include "core/model/Block.h"
#include "ui/ThemeController.h"

#include <wx/scrolwin.h>

#include <memory>

class wxPaintEvent;
class wxSizeEvent;

namespace litemark {

class DocumentView : public wxScrolledWindow {
public:
    explicit DocumentView(wxWindow* parent);
    ~DocumentView() override;

    void ShowPlaceholder();
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

    std::unique_ptr<Impl> impl_;
};

}  // namespace litemark
