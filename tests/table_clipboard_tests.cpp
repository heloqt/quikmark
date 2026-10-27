#include "core/document/TableClipboard.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "table_clipboard_tests: " << message << '\n';
        ++failures;
    }
}

litemark::TableCell Cell(const std::string& text, bool bold = false,
                         const std::string& href = {}) {
    litemark::TableCell cell;
    cell.spans.push_back(
        {text, bold, false, false, false, false, href});
    return cell;
}

litemark::Block TableBlock(bool header,
                           std::vector<std::vector<litemark::TableCell>> rows,
                           bool continuation = false) {
    litemark::Block block;
    block.kind = litemark::BlockKind::Table;
    block.tableFirstRowIsHeader = header;
    block.continuesPreviousBlock = continuation;
    block.tableRows = std::move(rows);
    return block;
}

void TestTableExport() {
    litemark::MarkdownDocument document;
    document.blocks.push_back(TableBlock(
        true,
        {
            {Cell("Name"), Cell("Status")},
            {Cell("Alpha"), Cell("Ready", true)},
            {Cell("One\nTwo"),
             Cell("Docs", false, "https://example.com/?a=1&b=2")},
            {Cell("A & <B>")},
        }));

    const auto data =
        litemark::BuildTableClipboardData(document, 0);
    Check(data.has_value(), "table clipboard data should be generated");
    if (!data) {
        return;
    }
    Check(data->plainText ==
              "Name\tStatus\nAlpha\tReady\nOne\nTwo\tDocs\n"
              "A & <B>\t",
          "table plain text should be TSV with empty trailing cells");
    Check(data->html.find("<th>Name</th><th>Status</th>") !=
              std::string::npos,
          "table HTML header mismatch");
    Check(data->html.find("<strong>Ready</strong>") !=
              std::string::npos,
          "table HTML should preserve bold text");
    Check(data->html.find(
              "href=\"https://example.com/?a=1&amp;b=2\"") !=
              std::string::npos,
          "table HTML should escape link attributes");
    Check(data->html.find("One<br>Two") != std::string::npos,
          "table HTML should preserve line breaks");
    Check(data->html.find("A &amp; &lt;B&gt;") != std::string::npos,
          "table HTML should escape cell text");
}

void TestSplitTableGroup() {
    litemark::MarkdownDocument document;
    document.blocks.push_back(TableBlock(
        true, {{Cell("H1"), Cell("H2")}, {Cell("A"), Cell("B")}}));
    document.blocks.push_back(TableBlock(
        false, {{Cell("C"), Cell("D")}}, true));

    litemark::Block paragraph;
    paragraph.kind = litemark::BlockKind::Text;
    paragraph.spans.push_back(
        {"after", false, false, false, false, false, ""});
    document.blocks.push_back(std::move(paragraph));
    document.blocks.push_back(TableBlock(
        true, {{Cell("Other"), Cell("Table")}}));

    const auto range = litemark::TableRangeForBlock(document, 1);
    Check(range && range->begin == 0 && range->end == 2,
          "continuation chunks should form one visual table");
    const auto data =
        litemark::BuildTableClipboardData(document, 1);
    Check(data && data->plainText == "H1\tH2\nA\tB\nC\tD",
          "split table should copy exactly once in row order");
    Check(data &&
              data->html.find("<tr><th>H1</th><th>H2</th></tr>") !=
                  std::string::npos,
          "split table should preserve the original header");
    Check(data &&
              data->html.find("<tr><th>C</th><th>D</th></tr>") ==
                  std::string::npos,
          "continuation rows must not become headers");
    Check(!litemark::TableRangeForBlock(document, 2).has_value(),
          "non-table blocks should not resolve to a table");
}

}  // namespace

int main() {
    TestTableExport();
    TestSplitTableGroup();
    if (failures != 0) {
        std::cerr << failures << " table clipboard test(s) failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
