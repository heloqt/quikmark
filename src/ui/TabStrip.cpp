#include "ui/TabStrip.h"

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/font.h>

#include <algorithm>
#include <cstddef>

namespace {

wxString FitLabel(wxDC& dc, const wxString& label, int maxWidth) {
    if (dc.GetTextExtent(label).x <= maxWidth) {
        return label;
    }

    const wxString suffix = wxString::FromUTF8("…");
    int low = 0;
    int high = static_cast<int>(label.length());
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        const wxString candidate = label.Left(middle) + suffix;
        if (dc.GetTextExtent(candidate).x <= maxWidth) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return label.Left(low) + suffix;
}

}  // namespace

namespace litemark {

TabStrip::TabStrip(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 44),
               wxBORDER_NONE) {
    palette_.windowBackground = wxColour("#F7F8FA");
    palette_.documentBackground = wxColour("#FFFFFF");
    palette_.text = wxColour("#24292F");
    palette_.mutedText = wxColour("#57606A");
    palette_.border = wxColour("#D0D7DE");
    palette_.codeBackground = wxColour("#F6F8FA");
    palette_.accent = wxColour("#0969DA");
    palette_.selection = wxColour("#B6D7FF");

    SetMinSize(wxSize(-1, FromDIP(44)));
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &TabStrip::OnPaint, this);
    Bind(wxEVT_MOTION, &TabStrip::OnMouseMotion, this);
    Bind(wxEVT_LEAVE_WINDOW, &TabStrip::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &TabStrip::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &TabStrip::OnLeftUp, this);
    Bind(wxEVT_SIZE, &TabStrip::OnSize, this);
}

void TabStrip::SetTabs(const std::vector<wxString>& labels, int selected) {
    labels_ = labels;
    selected_ = selected;
    RebuildGeometry();
    Refresh();
}

void TabStrip::SetOnSelect(std::function<void(int)> onSelect) {
    onSelect_ = std::move(onSelect);
}

void TabStrip::SetOnClose(std::function<void(int)> onClose) {
    onClose_ = std::move(onClose);
}

void TabStrip::ApplyTheme(const ThemePalette& palette) {
    palette_ = palette;
    SetBackgroundColour(palette.windowBackground);
    Refresh();
}

void TabStrip::RebuildGeometry() {
    geometry_.clear();
    if (labels_.empty() || GetClientSize().x <= 0) {
        return;
    }

    wxClientDC dc(this);
    wxFont normalFont(wxFontInfo(10).FaceName("Microsoft YaHei UI"));
    wxFont selectedFont = normalFont;
    selectedFont.SetWeight(wxFONTWEIGHT_BOLD);

    const int outerInset = FromDIP(12);
    const int verticalInset = FromDIP(5);
    const int gap = FromDIP(4);
    const int closeSize = FromDIP(22);
    const int horizontalPadding = FromDIP(11);
    const int minTabWidth = FromDIP(104);
    const int maxTabWidth = FromDIP(230);
    const int tabHeight = std::max(FromDIP(30), GetClientSize().y - verticalInset * 2);
    int x = outerInset;

    for (std::size_t i = 0; i < labels_.size(); ++i) {
        dc.SetFont(static_cast<int>(i) == selected_ ? selectedFont : normalFont);
        const int textWidth = dc.GetTextExtent(labels_[i]).x;
        int tabWidth = std::clamp(textWidth + closeSize + horizontalPadding * 2 + 8,
                                  minTabWidth, maxTabWidth);
        const int remaining = GetClientSize().x - outerInset - x;
        if (remaining < FromDIP(42)) {
            break;
        }
        tabWidth = std::min(tabWidth, remaining);

        const wxRect tab(x, verticalInset, tabWidth, tabHeight);
        const wxRect close(tab.GetRight() - closeSize - FromDIP(5),
                           tab.y + (tab.height - closeSize) / 2,
                           closeSize, closeSize);
        const int maxTextWidth =
            std::max(10, close.x - tab.x - horizontalPadding - FromDIP(5));

        TabGeometry item;
        item.tab = tab;
        item.closeButton = close;
        item.label = FitLabel(dc, labels_[i], maxTextWidth);
        geometry_.push_back(std::move(item));
        x += tabWidth + gap;
    }
}

void TabStrip::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    const wxSize client = GetClientSize();

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(palette_.windowBackground));
    dc.DrawRectangle(0, 0, client.x, client.y);

    if (geometry_.size() != labels_.size()) {
        RebuildGeometry();
    }

    wxFont normalFont(wxFontInfo(10).FaceName("Microsoft YaHei UI"));
    wxFont selectedFont = normalFont;
    selectedFont.SetWeight(wxFONTWEIGHT_BOLD);

    for (std::size_t i = 0; i < geometry_.size(); ++i) {
        const TabGeometry& item = geometry_[i];
        const bool selected = static_cast<int>(i) == selected_;
        const bool hovered = static_cast<int>(i) == hoveredTab_;

        wxColour background = palette_.windowBackground;
        if (selected) {
            background = palette_.documentBackground;
        } else if (hovered) {
            background = palette_.codeBackground;
        }

        dc.SetPen(wxPen(palette_.border, 1));
        dc.SetBrush(wxBrush(background));
        dc.DrawRoundedRectangle(item.tab, FromDIP(6));

        if (selected) {
            dc.SetPen(wxPen(palette_.accent, 2));
            dc.DrawLine(item.tab.x + FromDIP(8),
                        item.tab.GetBottom() - 1,
                        item.tab.GetRight() - FromDIP(8),
                        item.tab.GetBottom() - 1);
        }

        dc.SetFont(selected ? selectedFont : normalFont);
        dc.SetTextForeground(selected ? palette_.text : palette_.mutedText);
        const int textX = item.tab.x + FromDIP(11);
        const int textY =
            item.tab.y + (item.tab.height - dc.GetCharHeight()) / 2;
        dc.SetClippingRegion(item.tab.x + 2, item.tab.y,
                             item.closeButton.GetLeft() - item.tab.x - 5,
                             item.tab.height);
        dc.DrawText(item.label, textX, textY);
        dc.DestroyClippingRegion();

        const wxPoint center(
            item.closeButton.x + item.closeButton.width / 2,
            item.closeButton.y + item.closeButton.height / 2);
        if (hovered) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(palette_.codeBackground));
            dc.DrawCircle(center, FromDIP(9));
        }

        const int half = FromDIP(4);
        dc.SetPen(wxPen(hovered ? palette_.text : palette_.mutedText,
                        FromDIP(1)));
        dc.DrawLine(center.x - half, center.y - half,
                    center.x + half, center.y + half);
        dc.DrawLine(center.x + half, center.y - half,
                    center.x - half, center.y + half);
    }

    dc.SetPen(wxPen(palette_.border, 1));
    dc.DrawLine(0, client.y - 1, client.x, client.y - 1);
}

void TabStrip::OnMouseMotion(wxMouseEvent& event) {
    const int hovered = HitTab(event.GetPosition());
    if (hovered != hoveredTab_) {
        hoveredTab_ = hovered;
        Refresh();
    }
}

void TabStrip::OnMouseLeave(wxMouseEvent&) {
    if (pressedClose_ < 0) {
        hoveredTab_ = -1;
        Refresh();
    }
}

void TabStrip::OnLeftDown(wxMouseEvent& event) {
    const int closeIndex = HitClose(event.GetPosition());
    if (closeIndex >= 0) {
        pressedClose_ = closeIndex;
        if (!HasCapture()) {
            CaptureMouse();
        }
        Refresh();
        return;
    }

    const int tabIndex = HitTab(event.GetPosition());
    if (tabIndex >= 0 && onSelect_) {
        onSelect_(tabIndex);
    }
}

void TabStrip::OnLeftUp(wxMouseEvent& event) {
    const int pressed = pressedClose_;
    pressedClose_ = -1;
    if (HasCapture()) {
        ReleaseMouse();
    }
    if (pressed >= 0 && HitClose(event.GetPosition()) == pressed && onClose_) {
        onClose_(pressed);
    }
    Refresh();
}

void TabStrip::OnSize(wxSizeEvent& event) {
    RebuildGeometry();
    Refresh();
    event.Skip();
}

int TabStrip::HitTab(const wxPoint& point) const {
    for (std::size_t i = 0; i < geometry_.size(); ++i) {
        if (geometry_[i].tab.Contains(point)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int TabStrip::HitClose(const wxPoint& point) const {
    for (std::size_t i = 0; i < geometry_.size(); ++i) {
        if (geometry_[i].closeButton.Contains(point)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace litemark
