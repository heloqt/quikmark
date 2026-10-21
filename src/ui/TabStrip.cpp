#include "ui/TabStrip.h"

#include <wx/button.h>
#include <wx/sizer.h>

namespace {

constexpr std::size_t kMaxLabelLength = 28;

}  // namespace

namespace litemark {

TabStrip::TabStrip(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 38)) {
    sizer_ = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(sizer_);
}

void TabStrip::SetTabs(const std::vector<wxString>& labels, int selected) {
    labels_ = labels;
    selected_ = selected;
    Rebuild();
}

void TabStrip::SetOnSelect(std::function<void(int)> onSelect) {
    onSelect_ = std::move(onSelect);
}

void TabStrip::ApplyTheme(const ThemePalette& palette) {
    palette_ = palette;
    SetBackgroundColour(palette.windowBackground);
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const bool selected = static_cast<int>(i) == selected_;
        buttons_[i]->SetBackgroundColour(selected ? palette.accent : palette.windowBackground);
        buttons_[i]->SetForegroundColour(selected ? *wxWHITE : palette.text);
        buttons_[i]->Refresh();
    }
    Refresh();
}

void TabStrip::Rebuild() {
    sizer_->Clear(true);
    buttons_.clear();

    for (std::size_t i = 0; i < labels_.size(); ++i) {
        auto* button = new wxButton(this, wxID_ANY, Shorten(labels_[i]),
                                    wxDefaultPosition, wxDefaultSize,
                                    wxBORDER_NONE | wxBU_EXACTFIT);
        button->SetMinSize(wxSize(90, 30));
        const bool selected = static_cast<int>(i) == selected_;
        button->SetBackgroundColour(selected ? palette_.accent : palette_.windowBackground);
        button->SetForegroundColour(selected ? *wxWHITE : palette_.text);
        button->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
            if (onSelect_) {
                onSelect_(static_cast<int>(i));
            }
        });
        buttons_.push_back(button);
        sizer_->Add(button, 0, wxRIGHT | wxTOP | wxBOTTOM, 3);
    }

    sizer_->Layout();
    Refresh();
}

wxString TabStrip::Shorten(const wxString& label) {
    if (label.length() <= kMaxLabelLength) {
        return label;
    }
    return label.Left(kMaxLabelLength - 1) + wxString::FromUTF8("…");
}

}  // namespace litemark
