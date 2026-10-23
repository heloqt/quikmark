#include "ui/OutlineToggleButton.h"

#include <wx/dcbuffer.h>
#include <wx/msw/wrapwin.h>

namespace litemark {

OutlineToggleButton::OutlineToggleButton(wxWindow* parent, wxWindowID id)
    : wxControl(parent, id, wxDefaultPosition, wxDefaultSize,
                wxBORDER_NONE | wxWANTS_CHARS) {
    SetInitialSize(FromDIP(wxSize(24, 48)));
    SetMinSize(FromDIP(wxSize(24, 48)));
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_HAND));
    palette_ = ThemeController::BuildPalette(false);
    Bind(wxEVT_PAINT, &OutlineToggleButton::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &OutlineToggleButton::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &OutlineToggleButton::OnLeftUp, this);
    Bind(wxEVT_ENTER_WINDOW, &OutlineToggleButton::OnEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &OutlineToggleButton::OnLeave, this);
    Bind(wxEVT_KEY_DOWN, &OutlineToggleButton::OnKeyDown, this);
    SetExpanded(true);
}

void OutlineToggleButton::SetExpanded(bool expanded) {
    expanded_ = expanded;
#ifdef __WXMSW__
    ::SetWindowTextW(
        static_cast<HWND>(GetHandle()),
        expanded_ ? L"\x25C0" : L"\x25B6");
#endif
    SetToolTip(expanded ? wxString::FromUTF8("收起目录")
                        : wxString::FromUTF8("展开目录"));
    Refresh();
}

void OutlineToggleButton::ApplyTheme(const ThemePalette& palette) {
    palette_ = palette;
    Refresh();
}

void OutlineToggleButton::NotifyClick() {
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}

void OutlineToggleButton::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    const wxColour background =
        hovered_ ? palette_.codeBackground : palette_.documentBackground;
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(background));
    dc.DrawRectangle(0, 0, size.x, size.y);

    const int centerX = size.x / 2;
    const int centerY = size.y / 2;
    const int halfWidth = FromDIP(4);
    const int halfHeight = FromDIP(6);
    wxPoint points[3];
    if (expanded_) {
        points[0] = wxPoint(centerX + halfWidth, centerY - halfHeight);
        points[1] = wxPoint(centerX + halfWidth, centerY + halfHeight);
        points[2] = wxPoint(centerX - halfWidth, centerY);
    } else {
        points[0] = wxPoint(centerX - halfWidth, centerY - halfHeight);
        points[1] = wxPoint(centerX - halfWidth, centerY + halfHeight);
        points[2] = wxPoint(centerX + halfWidth, centerY);
    }
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(hovered_ ? palette_.accent : palette_.mutedText));
    dc.DrawPolygon(3, points);
}

void OutlineToggleButton::OnLeftDown(wxMouseEvent&) {
    pressed_ = true;
    if (!HasCapture()) {
        CaptureMouse();
    }
}

void OutlineToggleButton::OnLeftUp(wxMouseEvent& event) {
    if (pressed_ && GetClientRect().Contains(event.GetPosition())) {
        pressed_ = false;
        if (HasCapture()) {
            ReleaseMouse();
        }
        NotifyClick();
        return;
    }
    pressed_ = false;
    if (HasCapture()) {
        ReleaseMouse();
    }
}

void OutlineToggleButton::OnEnter(wxMouseEvent& event) {
    hovered_ = true;
    Refresh();
    event.Skip();
}

void OutlineToggleButton::OnLeave(wxMouseEvent& event) {
    hovered_ = false;
    Refresh();
    event.Skip();
}

void OutlineToggleButton::OnKeyDown(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_SPACE ||
        event.GetKeyCode() == WXK_RETURN) {
        NotifyClick();
        return;
    }
    event.Skip();
}

#ifdef __WXMSW__
WXLRESULT OutlineToggleButton::MSWWindowProc(
    WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
    if (message == BM_CLICK) {
        NotifyClick();
        return 0;
    }
    return wxControl::MSWWindowProc(message, wParam, lParam);
}
#endif

}  // namespace litemark
