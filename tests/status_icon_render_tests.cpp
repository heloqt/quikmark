#include "ui/FontCatalog.h"
#include "ui/StatusIconRenderer.h"
#include "ui/TextLayout.h"

#include <wx/app.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/init.h>

#include <cstdlib>
#include <iostream>

namespace {

#ifndef __WXMSW__
class RenderTestApp : public wxApp {
public:
    bool OnInit() override {
        return true;
    }
};
#endif

int CountBackground(const wxImage& image, const wxRect& area,
                    const wxColour& background) {
    int count = 0;
    for (int y = area.GetTop(); y <= area.GetBottom(); ++y) {
        for (int x = area.GetLeft(); x <= area.GetRight(); ++x) {
            if (std::abs(image.GetRed(x, y) - background.Red()) <= 3 &&
                std::abs(image.GetGreen(x, y) - background.Green()) <= 3 &&
                std::abs(image.GetBlue(x, y) - background.Blue()) <= 3) {
                ++count;
            }
        }
    }
    return count;
}

int CountGreen(const wxImage& image, const wxRect& area) {
    int count = 0;
    for (int y = area.GetTop(); y <= area.GetBottom(); ++y) {
        for (int x = area.GetLeft(); x <= area.GetRight(); ++x) {
            const int red = image.GetRed(x, y);
            const int green = image.GetGreen(x, y);
            const int blue = image.GetBlue(x, y);
            if (green > red + 40 && green > blue + 40) {
                ++count;
            }
        }
    }
    return count;
}

int RunTests() {
    wxInitAllImageHandlers();

    wxBitmap layoutBitmap(600, 80, 32);
    wxMemoryDC layoutDc(layoutBitmap);
    litemark::detail::TextStyle layoutStyle;
    std::vector<litemark::InlineSpan> spans(1);
    spans[0].text = wxString::FromUTF8("✅ 完成").ToUTF8().data();
    litemark::detail::TextLayout layout;
    litemark::detail::BuildTextLayout(
        layoutDc, layout, 560, spans, layoutStyle, 1.0, nullptr);
    if (layout.lines.size() != 1 || layout.lines[0].segments.empty() ||
        !layout.lines[0].segments[0].statusIcon) {
        std::cerr << "status_icon_render_tests: layout did not mark icon\n";
        return 1;
    }
    layoutDc.SelectObject(wxNullBitmap);

    const wxColour background(24, 32, 40);
    wxBitmap bitmap(220, 100, 32);
    wxMemoryDC dc(bitmap);
    dc.SetBackground(wxBrush(background));
    dc.Clear();
    litemark::detail::StatusIconDrawCommand command;
    command.id = litemark::detail::StatusIconId::Check;
    command.fallbackText = wxString::FromUTF8("✅");
    command.fallbackFont =
        wxFont(wxFontInfo(24).FaceName(litemark::UiFaceName()));
    command.colour = wxColour(230, 230, 230);
    command.x = 20;
    command.y = 18;
    command.width = 64;
    command.height = 64;
    litemark::detail::StatusIconDrawCommand warningCommand = command;
    warningCommand.id = litemark::detail::StatusIconId::Warning;
    warningCommand.fallbackText = wxString::FromUTF8("⚠️");
    warningCommand.x = 120;
    litemark::detail::StatusIconAsset checkAsset;
    if (!litemark::detail::GetStatusIconAsset(
            litemark::detail::StatusIconId::Check, &checkAsset)) {
        std::cerr << "status_icon_render_tests: missing check asset\n";
        return 1;
    }
    if (!litemark::detail::DrawStatusIconBatch(
            dc, {command, warningCommand})) {
        std::cerr << "status_icon_render_tests: batch render failed\n";
        return 1;
    }
    if (litemark::detail::DrawStatusIconAwareText(
            dc, wxString::FromUTF8("🙂"), command.fallbackFont,
            command.colour, 5, 5, 20)) {
        std::cerr << "status_icon_render_tests: unknown icon was claimed\n";
        return 1;
    }
    dc.SelectObject(wxNullBitmap);

    const wxImage image = bitmap.ConvertToImage();
    const wxRect iconArea(20, 18, 64, 64);
    const wxRect warningArea(120, 18, 64, 64);
    const int green = CountGreen(image, iconArea);
    const int backgroundPixels =
        CountBackground(image, warningArea, background);
    if (green < 100 || backgroundPixels < 500) {
        std::cerr << "status_icon_render_tests: invalid icon render: green="
                  << green << " background=" << backgroundPixels << '\n';
        return 1;
    }

    std::cout << "status_icon_render_tests: passed (" << green
              << " green pixels)\n";
    return 0;
}

}  // namespace

#ifdef __WXMSW__
int main() {
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "status_icon_render_tests: wx initialization failed\n";
        return 1;
    }
    return RunTests();
}
#else
wxIMPLEMENT_APP_NO_MAIN(RenderTestApp);
int main(int argc, char** argv) {
    if (!wxEntryStart(argc, argv)) {
        std::cerr << "status_icon_render_tests: wx initialization failed\n";
        return 1;
    }
    if (!wxTheApp->CallOnInit()) {
        std::cerr << "status_icon_render_tests: wx app initialization failed\n";
        wxEntryCleanup();
        return 1;
    }
    const int result = RunTests();
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
#endif
