#include "app/LanguageController.h"

#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/intl.h>
#include <wx/utils.h>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "language_controller_tests: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "language_controller_tests: initialization failed\n";
        return EXIT_FAILURE;
    }

    using litemark::LanguageController;
    using litemark::LanguageMode;
    Check(LanguageController::ParseMode("system") == LanguageMode::System,
          "system preference mismatch");
    Check(LanguageController::ParseMode(" EN ") == LanguageMode::English,
          "English preference mismatch");
    Check(LanguageController::ParseMode("zh_CN") ==
              LanguageMode::SimplifiedChinese,
          "Chinese preference mismatch");
    Check(LanguageController::ParseMode("unknown") ==
              LanguageMode::System,
          "unknown preference should fall back to system");

    const wxString sourceRoot = wxGetCwd();
    const wxString settingsPath = wxFileName(
        wxFileName::GetTempDir(),
        wxString::Format("quikmark-language-%lu.ini", wxGetProcessId()))
                                      .GetFullPath();
    wxRemoveFile(settingsPath);
    LanguageController controller(
        settingsPath,
        sourceRoot + wxFileName::GetPathSeparator() + "build" +
            wxFileName::GetPathSeparator() + "bin" +
            wxFileName::GetPathSeparator() + "quikmark.exe",
        sourceRoot);

    controller.SetMode(LanguageMode::SimplifiedChinese);
    Check(wxGetTranslation("File") == wxString::FromUTF8("文件"),
          "Chinese menu catalog was not loaded");
    Check(wxGetTranslation("System") == wxString::FromUTF8("跟随系统"),
          "Chinese appearance translation mismatch");
    Check(wxGetTranslation("Outline") == wxString::FromUTF8("目录"),
          "Chinese outline translation mismatch");
    Check(wxGetTranslation("Type to search") ==
              wxString::FromUTF8("输入内容后开始查找"),
          "Chinese find-bar translation mismatch");
    Check(wxGetTranslation("Image unavailable") ==
              wxString::FromUTF8("图片不可用"),
          "Chinese media placeholder translation mismatch");
    Check(wxGetTranslation("Collapse outline") ==
              wxString::FromUTF8("收起目录"),
          "Chinese outline tooltip translation mismatch");
    Check(wxGetTranslation("OK") == wxString::FromUTF8("确定"),
          "wxWidgets Chinese standard catalog was not loaded");
    Check(wxGetTranslation(
              "Native, lightweight Markdown reader\n"
              "Fast opening, focused reading, no editing\n"
              "\n"
              "Build: ") ==
              wxString::FromUTF8(
                  "原生、轻量的 Markdown 阅读器\n"
                  "快速打开，专注阅读，不提供编辑功能\n"
                  "\n"
                  "构建："),
          "Chinese About translation mismatch");
    Check(controller.HelpLocaleCode() == "zh_CN",
          "Chinese help locale mismatch");

    controller.SetMode(LanguageMode::English);
    Check(wxGetTranslation("File") == "File",
          "English menu should use source strings");
    Check(controller.HelpLocaleCode() == "en",
          "English help locale mismatch");

    wxFile settings(settingsPath);
    Check(settings.IsOpened(), "language preference file was not saved");
    if (settings.IsOpened()) {
        wxString contents;
        settings.ReadAll(&contents);
        Check(contents.Contains("language=en"),
              "language preference was not persisted");
    }

    wxRemoveFile(settingsPath);
    if (failures != 0) {
        std::cerr << "language_controller_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "language_controller_tests: passed\n";
    return EXIT_SUCCESS;
}
