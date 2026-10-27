#include "ui/ThemeController.h"

#include "app/UserData.h"

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/settings.h>
#include <wx/stdpaths.h>

namespace {

constexpr const char* kAppearanceKey = "/appearance";

wxString PreferencePath() {
    return litemark::app::ConfigFilePath("settings.ini");
}

}  // namespace

namespace litemark {

ThemeController::ThemeController() {
    LoadPreference();
    ApplyNativeAppearance();
    RebuildPalette();
}

bool ThemeController::IsDark() const {
    if (mode_ == ThemeMode::Dark) {
        return true;
    }
    if (mode_ == ThemeMode::Light) {
        return false;
    }
    return wxSystemSettings::GetAppearance().AreAppsDark();
}

void ThemeController::SetOnChanged(std::function<void()> onChanged) {
    onChanged_ = std::move(onChanged);
}

void ThemeController::SetMode(ThemeMode mode) {
    mode_ = mode;
    ApplyNativeAppearance();
    SavePreference();
    Refresh();
}

void ThemeController::Refresh() {
    RebuildPalette();
    if (onChanged_) {
        onChanged_();
    }
}

wxString ThemeController::DisplayName(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::System:
            return wxGetTranslation("System");
        case ThemeMode::Light:
            return wxGetTranslation("Light");
        case ThemeMode::Dark:
            return wxGetTranslation("Dark");
    }
    return wxGetTranslation("System");
}

ThemeMode ThemeController::ParseMode(const wxString& value) {
    const wxString normalized = value.Lower().Trim().Trim(false);
    if (normalized == "light") {
        return ThemeMode::Light;
    }
    if (normalized == "dark") {
        return ThemeMode::Dark;
    }
    return ThemeMode::System;
}

void ThemeController::LoadPreference() {
    wxFileConfig config(wxString(), wxString(), PreferencePath(), wxString(),
                        wxCONFIG_USE_LOCAL_FILE);
    wxString value;
    config.Read(kAppearanceKey, &value, "system");

    mode_ = ParseMode(value);
}

void ThemeController::SavePreference() const {
    wxFileConfig config(wxString(), wxString(), PreferencePath(), wxString(),
                        wxCONFIG_USE_LOCAL_FILE);
    const char* value = "system";
    if (mode_ == ThemeMode::Light) {
        value = "light";
    } else if (mode_ == ThemeMode::Dark) {
        value = "dark";
    }
    config.Write(kAppearanceKey, wxString::FromUTF8(value));
    config.Flush();
}

void ThemeController::ApplyNativeAppearance() {
    wxApp::Appearance appearance = wxApp::Appearance::System;
    if (mode_ == ThemeMode::Light) {
        appearance = wxApp::Appearance::Light;
    } else if (mode_ == ThemeMode::Dark) {
        appearance = wxApp::Appearance::Dark;
    }

    if (wxTheApp != nullptr) {
        static_cast<wxApp*>(wxTheApp)->SetAppearance(appearance);
    }
}

void ThemeController::RebuildPalette() {
    palette_ = BuildPalette(IsDark());
}

ThemePalette ThemeController::BuildPalette(bool dark) {
    if (dark) {
        return {
            wxColour("#202124"),
            wxColour("#1E1E1E"),
            wxColour("#D4D4D4"),
            wxColour("#9AA0A6"),
            wxColour("#66B2FF"),
            wxColour("#3C4043"),
            wxColour("#252526"),
            wxColour("#5F6368"),
            wxColour("#264F78"),
            wxColour("#66B2FF"),
        };
    }
    return {
        wxColour("#F7F8FA"),
        wxColour("#FFFFFF"),
        wxColour("#24292F"),
        wxColour("#57606A"),
        wxColour("#0969DA"),
        wxColour("#D0D7DE"),
        wxColour("#F6F8FA"),
        wxColour("#D0D7DE"),
        wxColour("#B6D7FF"),
        wxColour("#0969DA"),
    };
}

}  // namespace litemark
