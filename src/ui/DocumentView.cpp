#include "ui/DocumentView.h"

#include <wx/font.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

namespace {

wxString PlaceholderText() {
    return wxString::FromUTF8(
        "liteMark\n\n"
        "这是一个只读 Markdown 阅读器骨架。\n"
        "使用“文件 > 打开”或把 Markdown 文件拖入窗口。\n\n"
        "当前版本先验证窗口、Tab、主题和 UTF-8 文件读取。");
}

}  // namespace

namespace litemark {

DocumentView::DocumentView(wxWindow* parent)
    : wxPanel(parent, wxID_ANY) {
    text_ = new wxTextCtrl(this, wxID_ANY, PlaceholderText(), wxDefaultPosition,
                           wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 |
                               wxTE_NOHIDESEL | wxBORDER_NONE);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text_, 1, wxEXPAND);
    SetSizer(sizer);

    wxFont font(wxFontInfo(11).FaceName("Microsoft YaHei UI"));
    if (font.IsOk()) {
        text_->SetFont(font);
    }
    text_->SetInsertionPoint(0);
}

void DocumentView::ShowPlaceholder() {
    text_->ChangeValue(PlaceholderText());
    text_->SetInsertionPoint(0);
    text_->ShowPosition(0);
}

void DocumentView::SetContent(const wxString& text, int scrollY) {
    text_->ChangeValue(text);
    text_->SetInsertionPoint(0);
    text_->ShowPosition(0);
    if (scrollY > 0) {
        text_->ScrollLines(scrollY);
    }
}

int DocumentView::ScrollY() const {
    return text_->GetScrollPos(wxVERTICAL);
}

void DocumentView::ApplyTheme(const ThemePalette& palette) {
    SetBackgroundColour(palette.documentBackground);
    text_->SetBackgroundColour(palette.documentBackground);
    text_->SetForegroundColour(palette.text);
    Refresh();
}

}  // namespace litemark
