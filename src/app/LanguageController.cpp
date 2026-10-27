#include "app/LanguageController.h"

#include <wx/fileconf.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/translation.h>
#include <wx/uilocale.h>
#include <wx/utils.h>

#include <algorithm>

namespace {

constexpr const char* kLanguageKey = "/language";
constexpr const char* kCatalogDomain = "quikmark";

wxString Normalize(const wxString& path) {
    wxFileName file(path);
    file.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return file.GetFullPath();
}

}  // namespace

namespace litemark {

LanguageController::LanguageController(
    const wxString& settingsPath,
    const wxString& executablePath,
    const wxString& currentDirectory)
    : settingsPath_(settingsPath),
      executablePath_(executablePath),
      currentDirectory_(currentDirectory) {
    LoadPreference();
    ApplyLanguage();
}

bool LanguageController::IsSimplifiedChinese() const {
    return language_ == wxLANGUAGE_CHINESE_SIMPLIFIED ||
           language_ == wxLANGUAGE_CHINESE_SINGAPORE;
}

wxString LanguageController::HelpLocaleCode() const {
    return IsSimplifiedChinese() ? "zh_CN" : "en";
}

void LanguageController::SetMode(LanguageMode mode) {
    mode_ = mode;
    SavePreference();
    ApplyLanguage();
}

LanguageMode LanguageController::ParseMode(const wxString& value) {
    const wxString normalized = value.Lower().Trim().Trim(false);
    if (normalized == "en" || normalized == "en_us") {
        return LanguageMode::English;
    }
    if (normalized == "zh" || normalized == "zh_cn" ||
        normalized == "zh_hans") {
        return LanguageMode::SimplifiedChinese;
    }
    return LanguageMode::System;
}

wxString LanguageController::SerializeMode(LanguageMode mode) {
    switch (mode) {
        case LanguageMode::English:
            return "en";
        case LanguageMode::SimplifiedChinese:
            return "zh_CN";
        case LanguageMode::System:
            break;
    }
    return "system";
}

bool LanguageController::IsChineseSystemLanguage() {
    const wxString language =
        wxUILocale::GetSystemLocaleId().GetLanguage().Lower();
    return language.StartsWith("zh");
}

std::vector<wxString> LanguageController::LocaleLookupRoots(
    const wxString& executablePath,
    const wxString& currentDirectory) {
    const wxFileName executable(executablePath);
    const wxString executableDirectory = executable.GetPath();
    const wxString separator(1, wxFileName::GetPathSeparator());
    std::vector<wxString> roots;
    wxString configured;
    if (wxGetEnv("QUIKMARK_LOCALE_DIR", &configured) &&
        !configured.empty()) {
        roots.push_back(Normalize(configured));
    }
    roots.push_back(
        Normalize(executableDirectory + separator + "locale"));
    roots.push_back(
        Normalize(executableDirectory + separator + ".." + separator +
                  "share" + separator + "quikmark" + separator + "locale"));
    roots.push_back(
        Normalize(executableDirectory + separator + ".." + separator +
                  ".." + separator + ".." + separator + "assets" +
                  separator + "locale"));
    roots.push_back(
        Normalize(currentDirectory + separator + "assets" + separator +
                  "locale"));
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    return roots;
}

void LanguageController::LoadPreference() {
    wxFileConfig config(wxString(), wxString(), settingsPath_, wxString(),
                        wxCONFIG_USE_LOCAL_FILE);
    wxString value;
    config.Read(kLanguageKey, &value, "system");
    mode_ = ParseMode(value);
}

void LanguageController::SavePreference() const {
    wxFileConfig config(wxString(), wxString(), settingsPath_, wxString(),
                        wxCONFIG_USE_LOCAL_FILE);
    config.Write(kLanguageKey, SerializeMode(mode_));
    config.Flush();
}

void LanguageController::ApplyLanguage() {
    if (mode_ == LanguageMode::English) {
        language_ = wxLANGUAGE_ENGLISH_US;
    } else if (mode_ == LanguageMode::SimplifiedChinese) {
        language_ = wxLANGUAGE_CHINESE_SIMPLIFIED;
    } else {
        language_ = IsChineseSystemLanguage()
                        ? wxLANGUAGE_CHINESE_SIMPLIFIED
                        : wxLANGUAGE_ENGLISH_US;
    }

    for (const wxString& root :
         LocaleLookupRoots(executablePath_, currentDirectory_)) {
        if (wxDirExists(root)) {
            wxFileTranslationsLoader::AddCatalogLookupPathPrefix(root);
        }
    }

    auto* translations = new wxTranslations();
    translations->SetLanguage(language_);
    if (IsSimplifiedChinese()) {
        translations->AddStdCatalog();
        translations->AddCatalog(kCatalogDomain,
                                 wxLANGUAGE_ENGLISH_US);
    }
    wxTranslations::Set(translations);
}

}  // namespace litemark
