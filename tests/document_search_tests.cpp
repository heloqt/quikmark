#include "core/document/DocumentSearch.h"
#include "core/document/DocumentText.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "document_search_tests: " << message << '\n';
        ++failures;
    }
}

litemark::MarkdownDocument SampleDocument() {
    litemark::MarkdownDocument document;
    litemark::Block first;
    first.kind = litemark::BlockKind::Text;
    first.spans.push_back(
        {"Alpha beta ALPHA.", false, false, false, false, false, ""});
    document.blocks.push_back(first);

    litemark::Block second;
    second.kind = litemark::BlockKind::Text;
    second.spans.push_back(
        {"中文内容 alpha", false, false, false, false, false, ""});
    document.blocks.push_back(second);
    return document;
}

void TestAsciiCaseInsensitive() {
    const auto document = SampleDocument();
    const auto result = litemark::FindDocumentText(document, "alpha");
    Check(result.matches.size() == 3, "should find three ASCII matches");
    Check(result.matches[0].blockIndex == 0 &&
              result.matches[0].utf8Offset == 0,
          "first match location mismatch");
    Check(result.matches[1].blockIndex == 0 &&
              result.matches[1].utf8Offset == 11,
          "uppercase ASCII match location mismatch");
}

void TestChineseSearch() {
    const auto document = SampleDocument();
    const auto result = litemark::FindDocumentText(document, "中文");
    Check(result.matches.size() == 1, "Chinese search should find one match");
    Check(result.matches[0].blockIndex == 1 &&
              result.matches[0].utf8Offset == 0 &&
              result.matches[0].utf8Length == 6,
          "Chinese UTF-8 match offsets mismatch");
}

void TestNoMatchAndEmptyQuery() {
    const auto document = SampleDocument();
    Check(litemark::FindDocumentText(document, "").matches.empty(),
          "empty query should return no matches");
    Check(litemark::FindDocumentText(document, "missing").matches.empty(),
          "missing text should return no matches");
}

void TestResultLimit() {
    auto document = SampleDocument();
    const auto result = litemark::FindDocumentText(document, "a", 2);
    Check(result.matches.size() == 2, "result count should respect the limit");
    Check(result.truncated, "result limit should mark the result as truncated");
}

void TestCancellation() {
    const auto document = SampleDocument();
    std::atomic_bool cancelled{true};
    const auto result =
        litemark::FindDocumentText(document, "alpha", 100, &cancelled);
    Check(result.matches.empty(), "cancelled search should return no matches");
}

}  // namespace

int main() {
    TestAsciiCaseInsensitive();
    TestChineseSearch();
    TestNoMatchAndEmptyQuery();
    TestResultLimit();
    TestCancellation();
    if (failures != 0) {
        std::cerr << "document_search_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "document_search_tests: 5 cases passed\n";
    return EXIT_SUCCESS;
}
