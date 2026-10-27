#include "core/document/DocumentText.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "document_text_tests: " << message << '\n';
        ++failures;
    }
}

litemark::MarkdownDocument SampleDocument() {
    litemark::MarkdownDocument document;

    litemark::Block heading;
    heading.kind = litemark::BlockKind::Heading;
    heading.headingLevel = 1;
    heading.spans.push_back({"标题", false, false, false, false, false, ""});
    document.blocks.push_back(heading);

    litemark::Block paragraph;
    paragraph.kind = litemark::BlockKind::Text;
    paragraph.spans.push_back({"First ", false, false, false, false, false, ""});
    paragraph.spans.push_back({"bold", true, false, false, false, false, ""});
    paragraph.spans.push_back({" line.", false, false, false, false, false, ""});
    document.blocks.push_back(paragraph);

    litemark::Block code;
    code.kind = litemark::BlockKind::Code;
    code.raw = "alpha\nbeta";
    document.blocks.push_back(code);

    litemark::Block table;
    table.kind = litemark::BlockKind::Table;
    table.tableRows = {
        {{{{"A", false, false, false, false, false, ""}}},
         {{{"B", false, false, false, false, false, ""}}}},
        {{{{"1", false, false, false, false, false, ""}}},
         {{{"2", false, false, false, false, false, ""}}}},
    };
    document.blocks.push_back(table);
    return document;
}

void TestPlainBlockText() {
    const auto document = SampleDocument();
    Check(litemark::BlockPlainText(document.blocks[0]) == "标题",
          "heading text mismatch");
    Check(litemark::BlockPlainText(document.blocks[1]) == "First bold line.",
          "inline spans should concatenate without markup");
    Check(litemark::BlockPlainText(document.blocks[2]) == "alpha\nbeta",
          "code plain text mismatch");
    Check(litemark::BlockPlainText(document.blocks[3]) == "A\tB\n1\t2",
          "table should copy in visual row order");
}

void TestTableCellTextRanges() {
    litemark::Block table;
    table.kind = litemark::BlockKind::Table;
    table.tableRows = {
        {{{{"A", false, false, false, false, false, ""}}},
         {{{"B", false, false, false, false, false, ""}}}},
        {{{{"你", false, false, false, false, false, ""}}}},
    };

    const auto ranges = litemark::TableCellTextRanges(table);
    Check(ranges.size() == 4, "table cell range count mismatch");
    Check(ranges[0].begin == 0 && ranges[0].end == 1,
          "first table cell range mismatch");
    Check(ranges[1].begin == 2 && ranges[1].end == 3,
          "second table cell range must skip the tab separator");
    Check(ranges[2].begin == 4 && ranges[2].end == 7,
          "UTF-8 table cell range mismatch");
    Check(ranges[3].begin == 7 && ranges[3].end == 7,
          "missing trailing table cell must be empty");
    Check(litemark::BlockPlainText(table).size() == 7,
          "table plain text length mismatch");
}

void TestPartialSameBlockSelection() {
    const auto document = SampleDocument();
    const auto text = litemark::PlainTextForSelection(
        document, {{1, 6}, {1, 10}});
    Check(text == "bold", "same-block UTF-8 selection mismatch");
}

void TestCrossBlockSelection() {
    const auto document = SampleDocument();
    const auto text = litemark::PlainTextForSelection(
        document, {{1, 6}, {2, 5}});
    Check(text == "bold line.\nalpha",
          "cross-block selection should join blocks with one newline");
}

void TestReversedSelection() {
    const auto document = SampleDocument();
    const auto forward = litemark::PlainTextForSelection(
        document, {{0, 0}, {2, 5}});
    const auto reversed = litemark::PlainTextForSelection(
        document, {{2, 5}, {0, 0}});
    Check(forward == reversed,
          "selection copied backward should match forward selection");
}

void TestUtf8BoundaryClamping() {
    const std::string text = "A你B";
    Check(litemark::ClampUtf8Offset(text, 2) == 1,
          "offset inside a UTF-8 character should clamp to its start");
    Check(litemark::ClampUtf8Offset(text, 999) == text.size(),
          "offset past the end should clamp to the end");
    Check(litemark::ClampUtf8Offset(text, 4) == 4,
          "valid UTF-8 character boundary should be retained");
}

void TestEmptySelectionAndFullDocument() {
    const auto document = SampleDocument();
    Check(litemark::IsEmptySelection(document, {{1, 3}, {1, 3}}),
          "identical points should be an empty selection");
    Check(!litemark::IsEmptySelection(document, {{1, 0}, {1, 1}}),
          "one character should be a non-empty selection");

    const auto text = litemark::PlainTextForSelection(
        document,
        {{0, 0}, {3, litemark::BlockPlainText(document.blocks[3]).size()}});
    Check(text.find("标题") == 0, "full selection should begin with heading text");
    Check(text.find("First bold line.") != std::string::npos,
          "full selection should contain paragraph text");
    Check(text.find("alpha\nbeta") != std::string::npos,
          "full selection should contain code text");
    Check(text.find("A\tB\n1\t2") != std::string::npos,
          "full selection should contain table text");
}

void TestParagraphFragmentsCopyWithoutInsertedNewline() {
    litemark::MarkdownDocument document;
    litemark::Block first;
    first.kind = litemark::BlockKind::Text;
    first.spans.push_back({"alpha ", false, false, false, false, false, ""});
    document.blocks.push_back(first);

    litemark::Block second;
    second.kind = litemark::BlockKind::Text;
    second.continuesPreviousBlock = true;
    second.spans.push_back({"beta", false, false, false, false, false, ""});
    document.blocks.push_back(second);

    const auto text = litemark::PlainTextForSelection(
        document, {{0, 0}, {1, 4}});
    Check(text == "alpha beta",
          "paragraph fragments must not add a synthetic newline");
}

}  // namespace

int main() {
    TestPlainBlockText();
    TestTableCellTextRanges();
    TestPartialSameBlockSelection();
    TestCrossBlockSelection();
    TestReversedSelection();
    TestUtf8BoundaryClamping();
    TestEmptySelectionAndFullDocument();
    TestParagraphFragmentsCopyWithoutInsertedNewline();
    if (failures != 0) {
        std::cerr << "document_text_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "document_text_tests: 8 cases passed\n";
    return EXIT_SUCCESS;
}
