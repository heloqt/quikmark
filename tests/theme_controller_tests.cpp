#include "ui/ThemeController.h"

#include <wx/init.h>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "theme_controller_tests: " << message << '\n';
        ++failures;
    }
}

void TestModeParsing() {
    using litemark::ThemeController;
    using litemark::ThemeMode;
    Check(ThemeController::ParseMode("system") == ThemeMode::System,
          "system preference should map to system mode");
    Check(ThemeController::ParseMode("light") == ThemeMode::Light,
          "light preference should map to light mode");
    Check(ThemeController::ParseMode(" DARK ") == ThemeMode::Dark,
          "preference parsing should ignore case and surrounding spaces");
    Check(ThemeController::ParseMode("sepia") == ThemeMode::System,
          "unknown preference should fall back to system mode");
    Check(ThemeController::ParseMode("") == ThemeMode::System,
          "empty preference should fall back to system mode");
}

void TestDisplayNames() {
    using litemark::ThemeController;
    using litemark::ThemeMode;
    Check(ThemeController::DisplayName(ThemeMode::System) ==
              "System",
          "system display name mismatch");
    Check(ThemeController::DisplayName(ThemeMode::Light) ==
              "Light",
          "light display name mismatch");
    Check(ThemeController::DisplayName(ThemeMode::Dark) ==
              "Dark",
          "dark display name mismatch");
}

void TestLightPalette() {
    using litemark::ThemeController;
    const auto palette = ThemeController::BuildPalette(false);
    Check(palette.documentBackground == wxColour("#FFFFFF"),
          "light document background should be white");
    Check(palette.text == wxColour("#24292F"),
          "light text color mismatch");
    Check(palette.text != palette.documentBackground,
          "light text must contrast with the document background");
    Check(palette.link.IsOk() && palette.codeBackground.IsOk(),
          "light palette colors should be valid");
}

void TestDarkPalette() {
    using litemark::ThemeController;
    const auto palette = ThemeController::BuildPalette(true);
    Check(palette.documentBackground == wxColour("#1E1E1E"),
          "dark document background mismatch");
    Check(palette.text == wxColour("#D4D4D4"),
          "dark text color mismatch");
    Check(palette.text != palette.documentBackground,
          "dark text must contrast with the document background");
    Check(palette.link.IsOk() && palette.codeBackground.IsOk(),
          "dark palette colors should be valid");
}

void TestPaletteSelectionChanges() {
    using litemark::ThemeController;
    const auto light = ThemeController::BuildPalette(false);
    const auto dark = ThemeController::BuildPalette(true);
    Check(light.documentBackground != dark.documentBackground,
          "light and dark document backgrounds must differ");
    Check(light.text != dark.text,
          "light and dark text colors must differ");
}

}  // namespace

int main() {
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "theme_controller_tests: initialization failed\n";
        return EXIT_FAILURE;
    }

    TestModeParsing();
    TestDisplayNames();
    TestLightPalette();
    TestDarkPalette();
    TestPaletteSelectionChanges();
    if (failures != 0) {
        std::cerr << "theme_controller_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "theme_controller_tests: 5 cases passed\n";
    return EXIT_SUCCESS;
}
