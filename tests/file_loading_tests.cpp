#include "session/TabManager.h"

#include <wx/init.h>
#include <wx/string.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "file_loading_tests: " << message << '\n';
        ++failures;
    }
}

struct TempDirectory {
    std::filesystem::path path;

    TempDirectory() {
        const auto stamp =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("litemark-file-tests-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path File(const std::string& name) const {
        return path / name;
    }
};

wxString WxPath(const std::filesystem::path& path) {
    const std::string utf8 = path.u8string();
    return wxString::FromUTF8(utf8.data(), utf8.size());
}

void WriteBytes(const std::filesystem::path& path,
                const std::vector<unsigned char>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void TestPlainUtf8() {
    TempDirectory temp;
    const auto path = temp.File("plain.md");
    const std::string text = "# 标题\n\n中文正文\n";
    WriteBytes(path, std::vector<unsigned char>(text.begin(), text.end()));

    std::string content;
    std::shared_ptr<const litemark::MarkdownDocument> document;
    wxString error;
    const bool ok = litemark::TabManager::LoadFileAndParse(
        WxPath(path), nullptr, &content, &document, &error);
    Check(ok, "plain UTF-8 file should load");
    Check(content == text,
          "plain UTF-8 content mismatch");
    Check(document && !document->blocks.empty(),
          "loaded Markdown should be parsed");
    Check(error.empty(), "successful load should not report an error");
}

void TestUtf8Bom() {
    TempDirectory temp;
    const auto path = temp.File("bom.md");
    const std::string body = "# BOM title\n\nASCII body\n";
    std::vector<unsigned char> bytes = {0xEF, 0xBB, 0xBF};
    bytes.insert(bytes.end(), body.begin(), body.end());
    WriteBytes(path, bytes);

    std::string content;
    std::shared_ptr<const litemark::MarkdownDocument> document;
    wxString error;
    const bool ok = litemark::TabManager::LoadFileAndParse(
        WxPath(path), nullptr, &content, &document, &error);
    Check(ok, "UTF-8 BOM file should load");
    Check(content == body,
          "UTF-8 BOM should be removed from content");
}

void TestPartialDocumentIsPublishedBeforeFullParse() {
    TempDirectory temp;
    const auto path = temp.File("large.md");
    const std::string text =
        std::string(20000, 'a') + "\n\n# later\n\n" +
        std::string(20000, 'b') + "\n";
    WriteBytes(path, std::vector<unsigned char>(text.begin(), text.end()));

    std::string content;
    std::shared_ptr<const litemark::MarkdownDocument> document;
    wxString error;
    litemark::FileLoadTimings timings;
    int partialCalls = 0;
    std::size_t partialBlocks = 0;
    bool fullWasStillEmpty = false;
    const bool ok = litemark::TabManager::LoadFileAndParse(
        WxPath(path), nullptr, &content, &document, &error, &timings,
        [&](std::shared_ptr<const litemark::MarkdownDocument> partial) {
            ++partialCalls;
            fullWasStillEmpty = document == nullptr;
            partialBlocks = partial ? partial->blocks.size() : 0;
        });

    Check(ok, "large UTF-8 file should load");
    Check(partialCalls == 1,
          "large file should publish exactly one partial document");
    Check(fullWasStillEmpty,
          "partial document must be published before the full result");
    Check(partialBlocks > 0 && document &&
              document->blocks.size() >= partialBlocks,
          "partial document should be a non-empty prefix");
    Check(timings.prefixMd4cMicroseconds > 0,
          "prefix parse timing should be recorded");
    Check(timings.md4cMicroseconds > 0,
          "full md4c timing should be recorded");
}

void TestInvalidUtf8VariantsAreRejected() {
    struct Case {
        const char* name;
        std::vector<unsigned char> bytes;
    };
    const Case cases[] = {
        {"gbk.md", {0xD6, 0xD0, 0xCE, 0xC4}},
        {"invalid-continuation.md", {0xC3, 0x28}},
        {"truncated.md", {0xE2, 0x82}},
        {"overlong.md", {0xC0, 0x80}},
        {"surrogate.md", {0xED, 0xA0, 0x80}},
    };

    TempDirectory temp;
    for (const auto& item : cases) {
        const auto path = temp.File(item.name);
        WriteBytes(path, item.bytes);
        std::string content;
        std::shared_ptr<const litemark::MarkdownDocument> document;
        wxString error;
        const bool ok = litemark::TabManager::LoadFileAndParse(
            WxPath(path), nullptr, &content, &document, &error);
        Check(!ok, "invalid UTF-8 input must not be accepted");
        Check(!error.empty(), "invalid UTF-8 should report an error");
        Check(document == nullptr,
              "invalid UTF-8 must not produce a parsed document");
        Check(error.find("UTF-8") != wxString::npos,
              "invalid UTF-8 error should state the supported encoding");
    }
}

void TestEmptyFile() {
    TempDirectory temp;
    const auto path = temp.File("empty.md");
    WriteBytes(path, {});

    std::string content;
    std::shared_ptr<const litemark::MarkdownDocument> document;
    wxString error;
    const bool ok = litemark::TabManager::LoadFileAndParse(
        WxPath(path), nullptr, &content, &document, &error);
    Check(ok, "empty file should load without crashing");
    Check(content.empty(), "empty file content mismatch");
    Check(document && document->blocks.empty(),
          "empty file should produce an empty document");
}

void TestMissingFile() {
    TempDirectory temp;
    std::string content;
    std::shared_ptr<const litemark::MarkdownDocument> document;
    wxString error;
    const bool ok = litemark::TabManager::LoadFileAndParse(
        WxPath(temp.File("missing.md")), nullptr, &content, &document,
        &error);
    Check(!ok, "missing file should fail");
    Check(error == "File does not exist",
          "missing file should return a clear error");
}

void TestPreCancelledRead() {
    TempDirectory temp;
    const auto path = temp.File("cancel.md");
    const std::string text = "# cancel\n";
    WriteBytes(path, std::vector<unsigned char>(text.begin(), text.end()));

    std::string content;
    std::shared_ptr<const litemark::MarkdownDocument> document;
    wxString error;
    std::atomic_bool cancelled{true};
    const bool ok = litemark::TabManager::LoadFileAndParse(
        WxPath(path), &cancelled, &content, &document, &error);
    Check(!ok, "pre-cancelled file load should fail");
    Check(error == "Loading cancelled",
          "cancelled file load should report cancellation");
}

void TestOversizedFileIsRejectedBeforeReading() {
    TempDirectory temp;
    const auto path = temp.File("too-large.md");
    constexpr std::uintmax_t kMaxFileSize = 64ULL * 1024 * 1024;
    std::ofstream output(path, std::ios::binary);
    output.seekp(static_cast<std::streamoff>(kMaxFileSize));
    output.put('\0');
    output.close();

    std::string content;
    std::shared_ptr<const litemark::MarkdownDocument> document;
    wxString error;
    const bool ok = litemark::TabManager::LoadFileAndParse(
        WxPath(path), nullptr, &content, &document, &error);
    Check(!ok, "file above the 64 MiB limit should be rejected");
    Check(error.find(wxString::FromUTF8("64 MiB")) != wxString::npos,
          "oversized file error should include the safety limit");
}

}  // namespace

int main() {
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "file_loading_tests: wxWidgets initialization failed\n";
        return EXIT_FAILURE;
    }

    TestPlainUtf8();
    TestUtf8Bom();
    TestPartialDocumentIsPublishedBeforeFullParse();
    TestInvalidUtf8VariantsAreRejected();
    TestEmptyFile();
    TestMissingFile();
    TestPreCancelledRead();
    TestOversizedFileIsRejectedBeforeReading();
    if (failures != 0) {
        std::cerr << "file_loading_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "file_loading_tests: 8 cases passed\n";
    return EXIT_SUCCESS;
}
