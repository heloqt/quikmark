#pragma once

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/string.h>

#include <functional>

namespace litemark {

enum class ThemeMode {
    System,
    Light,
    Dark
};

struct ThemePalette {
    wxColour windowBackground;
    wxColour documentBackground;
    wxColour text;
    wxColour mutedText;
    wxColour link;
    wxColour border;
    wxColour codeBackground;
    wxColour quoteBorder;
    wxColour selection;
    wxColour accent;
};

class ThemeController {
public:
    ThemeController();

    ThemeMode mode() const { return mode_; }
    bool IsDark() const;
    const ThemePalette& palette() const { return palette_; }

    void SetOnChanged(std::function<void()> onChanged);
    void SetMode(ThemeMode mode);
    void Refresh();

    static ThemeMode ParseMode(const wxString& value);
    static wxString DisplayName(ThemeMode mode);
    static ThemePalette BuildPalette(bool dark);

private:
    void LoadPreference();
    void SavePreference() const;
    void ApplyNativeAppearance();
    void RebuildPalette();

    ThemeMode mode_ = ThemeMode::System;
    ThemePalette palette_;
    std::function<void()> onChanged_;
};

}  // namespace litemark
