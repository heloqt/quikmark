#pragma once

#include "ui/ThemeController.h"

#include <wx/control.h>

class wxKeyEvent;
class wxMouseEvent;
class wxPaintEvent;

namespace litemark {

class OutlineToggleButton : public wxControl {
public:
    OutlineToggleButton(wxWindow* parent, wxWindowID id);

    void SetExpanded(bool expanded);
    void ApplyTheme(const ThemePalette& palette);

protected:
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnEnter(wxMouseEvent& event);
    void OnLeave(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam,
                            WXLPARAM lParam) override;
#endif

private:
    void NotifyClick();

    ThemePalette palette_;
    bool expanded_ = true;
    bool hovered_ = false;
    bool pressed_ = false;
};

}  // namespace litemark
