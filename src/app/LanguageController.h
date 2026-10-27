#pragma once

#include <wx/intl.h>
#include <wx/string.h>

#include <vector>

namespace litemark {

enum class LanguageMode {
    System,
    English,
    SimplifiedChinese
};

class LanguageController {
public:
    LanguageController(const wxString& settingsPath,
                       const wxString& executablePath,
                       const wxString& currentDirectory);

    LanguageMode mode() const { return mode_; }
    wxLanguage effectiveLanguage() const { return language_; }
    bool IsSimplifiedChinese() const;
    wxString HelpLocaleCode() const;

    void SetMode(LanguageMode mode);

    static LanguageMode ParseMode(const wxString& value);
    static wxString SerializeMode(LanguageMode mode);
    static bool IsChineseSystemLanguage();
    static std::vector<wxString> LocaleLookupRoots(
        const wxString& executablePath,
        const wxString& currentDirectory);

private:
    void LoadPreference();
    void SavePreference() const;
    void ApplyLanguage();

    wxString settingsPath_;
    wxString executablePath_;
    wxString currentDirectory_;
    LanguageMode mode_ = LanguageMode::System;
    wxLanguage language_ = wxLANGUAGE_ENGLISH_US;
};

}  // namespace litemark
