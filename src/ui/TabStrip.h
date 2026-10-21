#pragma once

#include "ui/ThemeController.h"

#include <wx/panel.h>

#include <functional>
#include <vector>

class wxMouseEvent;
class wxPaintEvent;
class wxSizeEvent;

namespace litemark {

class TabStrip : public wxPanel {
public:
    explicit TabStrip(wxWindow* parent);

    void SetTabs(const std::vector<wxString>& labels, int selected);
    void SetOnSelect(std::function<void(int)> onSelect);
    void SetOnClose(std::function<void(int)> onClose);
    void ApplyTheme(const ThemePalette& palette);

private:
    struct TabGeometry {
        wxRect tab;
        wxRect closeButton;
        wxString label;
    };

    void RebuildGeometry();
    void OnPaint(wxPaintEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnSize(wxSizeEvent& event);
    int HitTab(const wxPoint& point) const;
    int HitClose(const wxPoint& point) const;

    std::vector<wxString> labels_;
    std::vector<TabGeometry> geometry_;
    int selected_ = -1;
    int hoveredTab_ = -1;
    int pressedClose_ = -1;
    ThemePalette palette_;
    std::function<void(int)> onSelect_;
    std::function<void(int)> onClose_;
};

}  // namespace litemark
