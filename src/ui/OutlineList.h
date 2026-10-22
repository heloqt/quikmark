#pragma once

#include "core/document/DocumentNavigation.h"
#include "ui/ThemeController.h"

#include <wx/vlbox.h>

#include <vector>

class wxMouseEvent;

namespace litemark {

class OutlineList : public wxVListBox {
public:
    explicit OutlineList(wxWindow* parent, wxWindowID id);

    void SetItems(const std::vector<HeadingAnchor>& headings);
    wxString SlugAt(std::size_t index) const;
    void ApplyTheme(const ThemePalette& palette);

protected:
    void OnDrawItem(wxDC& dc, const wxRect& rect,
                    std::size_t index) const override;
    wxCoord OnMeasureItem(std::size_t index) const override;
    void OnDrawBackground(wxDC& dc, const wxRect& rect,
                          std::size_t index) const override;

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam,
                            WXLPARAM lParam) override;
#endif

private:
    struct Item {
        wxString text;
        wxString slug;
        int level = 1;
    };

    void OnMouseWheel(wxMouseEvent& event);

    std::vector<Item> items_;
    ThemePalette palette_;
    double wheelAccumulator_ = 0.0;
};

}  // namespace litemark
