#include "ui/ColorEmojiRenderer.h"
#include "ui/TextLayout.h"

#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/init.h>

#include <cstdlib>
#include <iostream>

int main() {
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "emoji_render_tests: wx initialization failed\n";
        return 1;
    }
    wxInitAllImageHandlers();

    wxBitmap bitmap(600, 80, 32);
    wxMemoryDC dc(bitmap);
    const wxColour background(24, 32, 40);
    dc.SetBackground(wxBrush(background));
    dc.Clear();
    const wxFont font(
        wxFontInfo(24).FaceName("Microsoft YaHei UI"));
    const wxString text =
        wxString::FromUTF8("✅️  ❌️  ⌛️  ⏸️  ⚠️");
    wxBitmap layoutBitmap(600, 80, 32);
    wxMemoryDC layoutDc(layoutBitmap);
    litemark::detail::TextStyle layoutStyle;
    std::vector<litemark::InlineSpan> spans(1);
    spans[0].text = text.ToUTF8().data();
    litemark::detail::TextLayout layout;
    litemark::detail::BuildTextLayout(
        layoutDc, layout, 560, spans, layoutStyle, 1.0, nullptr);
    if (layout.lines.size() != 1 ||
        layout.lines[0].segments.empty() ||
        !layout.lines[0].segments[0].emoji) {
        std::cerr << "emoji_render_tests: layout did not mark emoji\n";
        return 1;
    }
    layoutDc.SelectObject(wxNullBitmap);

    std::vector<litemark::detail::EmojiDrawCommand> commands;
    litemark::detail::EmojiDrawCommand command;
    command.text = text;
    command.font = font;
    command.colour = wxColour(230, 230, 230);
    command.x = 12;
    command.y = 12;
    command.width = 520;
    command.height = 50;
    commands.push_back(command);
    const bool rendered =
        litemark::detail::DrawColorEmojiBatch(dc, commands);
    dc.SelectObject(wxNullBitmap);
    if (!rendered) {
        std::cerr << "emoji_render_tests: Direct2D text path unavailable\n";
        return 1;
    }

    const wxImage image = bitmap.ConvertToImage();
    int saturatedGreen = 0;
    int backgroundPixels = 0;
    for (int y = 0; y < image.GetHeight(); ++y) {
        for (int x = 0; x < image.GetWidth(); ++x) {
            const int red = image.GetRed(x, y);
            const int green = image.GetGreen(x, y);
            const int blue = image.GetBlue(x, y);
            if (green > red + 40 && green > blue + 40) {
                ++saturatedGreen;
            }
            if (x >= command.x && x < command.x + command.width &&
                y >= command.y && y < command.y + command.height &&
                std::abs(red - background.Red()) <= 3 &&
                std::abs(green - background.Green()) <= 3 &&
                std::abs(blue - background.Blue()) <= 3) {
                ++backgroundPixels;
            }
        }
    }
    if (saturatedGreen < 100 || backgroundPixels < 5000) {
        std::cerr << "emoji_render_tests: color glyph pixels missing: "
                  << saturatedGreen
                  << ", transparent background pixels: "
                  << backgroundPixels << '\n';
        return 1;
    }

    std::cout << "emoji_render_tests: passed ("
              << saturatedGreen << " saturated green pixels)\n";
    return 0;
}
