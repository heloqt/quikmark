#pragma once

#include "ui/ThemeController.h"

#include <wx/panel.h>

class wxTextCtrl;

namespace litemark {

class DocumentView : public wxPanel {
public:
    explicit DocumentView(wxWindow* parent);

    void ShowPlaceholder();
    void SetContent(const wxString& text, int scrollY = 0);
    int ScrollY() const;
    void ApplyTheme(const ThemePalette& palette);

private:
    wxTextCtrl* text_ = nullptr;
};

}  // namespace litemark
