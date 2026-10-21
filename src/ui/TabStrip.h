#pragma once

#include "ui/ThemeController.h"

#include <wx/panel.h>

#include <functional>
#include <vector>

class wxBoxSizer;
class wxButton;

namespace litemark {

class TabStrip : public wxPanel {
public:
    explicit TabStrip(wxWindow* parent);

    void SetTabs(const std::vector<wxString>& labels, int selected);
    void SetOnSelect(std::function<void(int)> onSelect);
    void ApplyTheme(const ThemePalette& palette);

private:
    void Rebuild();
    static wxString Shorten(const wxString& label);

    wxBoxSizer* sizer_ = nullptr;
    std::vector<wxButton*> buttons_;
    std::vector<wxString> labels_;
    int selected_ = -1;
    ThemePalette palette_;
    std::function<void(int)> onSelect_;
};

}  // namespace litemark
