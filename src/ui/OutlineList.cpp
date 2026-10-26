#include "ui/OutlineList.h"

#include "ui/ColorEmojiRenderer.h"
#include "ui/FontCatalog.h"

#include <wx/dc.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include <wx/settings.h>

#include <algorithm>
#include <cmath>

namespace litemark {

namespace {

#ifdef __WXMSW__
constexpr UINT kOutlineItemCountMessage = WM_APP + 701;
constexpr UINT kOutlineTopRowMessage = WM_APP + 702;
#endif

}  // namespace

OutlineList::OutlineList(wxWindow* parent, wxWindowID id)
    : wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize,
                 wxBORDER_NONE) {
    palette_.windowBackground = wxSystemSettings::GetColour(
        wxSYS_COLOUR_LISTBOX);
    palette_.documentBackground = palette_.windowBackground;
    palette_.text = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT);
    palette_.mutedText = palette_.text;
    palette_.selection = wxSystemSettings::GetColour(
        wxSYS_COLOUR_HIGHLIGHT);
    SetBackgroundColour(palette_.documentBackground);
    SetForegroundColour(palette_.text);
    Bind(wxEVT_MOUSEWHEEL, &OutlineList::OnMouseWheel, this);
}

void OutlineList::SetItems(const std::vector<HeadingAnchor>& headings) {
    items_.clear();
    items_.reserve(headings.size());
    for (const auto& heading : headings) {
        items_.push_back(Item{
            wxString::FromUTF8(heading.text.data(), heading.text.size()),
            wxString::FromUTF8(heading.slug.data(), heading.slug.size()),
            heading.level,
        });
    }
    wheelAccumulator_ = 0.0;
    SetItemCount(items_.size());
    SetSelection(wxNOT_FOUND);
    ScrollToRow(0);
    RefreshAll();
}

wxString OutlineList::SlugAt(std::size_t index) const {
    return index < items_.size() ? items_[index].slug : wxString();
}

void OutlineList::ApplyTheme(const ThemePalette& palette) {
    palette_ = palette;
    SetBackgroundColour(palette.documentBackground);
    SetForegroundColour(palette.text);
    SetSelectionBackground(palette.selection);
    RefreshAll();
}

void OutlineList::OnDrawItem(wxDC& dc, const wxRect& rect,
                             std::size_t index) const {
    if (index >= items_.size()) {
        return;
    }
    const Item& item = items_[index];
    wxFont font(wxFontInfo(item.level <= 2 ? 10 : 9)
                    .FaceName(UiFaceName()));
    if (item.level <= 2) {
        font.SetWeight(wxFONTWEIGHT_BOLD);
    }
    dc.SetFont(font);
    dc.SetTextForeground(palette_.text);
    const int indent = std::max(0, item.level - 1) * FromDIP(12);
    const int textY =
        rect.y + (rect.height - dc.GetCharHeight()) / 2;
    dc.SetClippingRegion(rect);
    const int textX = rect.x + FromDIP(10) + indent;
    if (!detail::DrawEmojiAwareText(
            dc, item.text, font, palette_.text, textX, textY,
            FromDIP(28))) {
        dc.DrawText(item.text, textX, textY);
    }
    dc.DestroyClippingRegion();
}

wxCoord OutlineList::OnMeasureItem(std::size_t) const {
    return FromDIP(28);
}

void OutlineList::OnDrawBackground(wxDC& dc, const wxRect& rect,
                                   std::size_t index) const {
    const bool selected = IsSelected(index);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(selected ? palette_.selection
                                 : palette_.documentBackground));
    dc.DrawRectangle(rect);
    if (selected) {
        dc.SetTextForeground(palette_.text);
    }
}

void OutlineList::OnMouseWheel(wxMouseEvent& event) {
    const int wheelDelta =
        event.GetWheelDelta() > 0 ? event.GetWheelDelta() : 120;
    const double notches =
        static_cast<double>(event.GetWheelRotation()) / wheelDelta;
    wheelAccumulator_ += notches * 3.0;
    const int lines = static_cast<int>(wheelAccumulator_);
    if (lines == 0) {
        return;
    }
    wheelAccumulator_ -= lines;
    ScrollRows(-lines);
}

#ifdef __WXMSW__
WXLRESULT OutlineList::MSWWindowProc(WXUINT message, WXWPARAM wParam,
                                     WXLPARAM lParam) {
    if (message == kOutlineItemCountMessage) {
        return static_cast<WXLRESULT>(items_.size());
    }
    if (message == kOutlineTopRowMessage) {
        return static_cast<WXLRESULT>(GetVisibleRowsBegin());
    }
    return wxVListBox::MSWWindowProc(message, wParam, lParam);
}
#endif

}  // namespace litemark
