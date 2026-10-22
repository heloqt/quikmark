#include "core/flow/Flowchart.h"
#include "core/parser/MarkdownParser.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "markdown_requirements_tests: " << message << '\n';
        ++failures;
    }
}

const litemark::Block* FindBlock(const litemark::MarkdownDocument& document,
                                 litemark::BlockKind kind) {
    for (const auto& block : document.blocks) {
        if (block.kind == kind) {
            return &block;
        }
    }
    return nullptr;
}

bool ContainsText(const litemark::Block& block, const std::string& text) {
    for (const auto& span : block.spans) {
        if (span.text.find(text) != std::string::npos) {
            return true;
        }
    }
    return block.raw.find(text) != std::string::npos;
}

bool HasStyle(const litemark::Block& block, const std::string& text,
              bool bold, bool italic, bool strikethrough, bool code,
              bool link = false) {
    for (const auto& span : block.spans) {
        if (span.text.find(text) != std::string::npos &&
            span.bold == bold && span.italic == italic &&
            span.strikethrough == strikethrough && span.code == code &&
            (!link || !span.href.empty())) {
            return true;
        }
    }
    return false;
}

void TestRequiredMarkdownBlocksAndStyles() {
    const std::string markdown =
        "# Heading\n\n"
        "Plain **bold** *italic* ~~deleted~~ `code` "
        "[link](https://example.com).\n\n"
        "> quoted\n\n"
        "- bullet\n\n"
        "3. third\n"
        "4. fourth\n\n"
        "- [x] done\n"
        "- [ ] todo\n\n"
        "---\n\n"
        "```cpp\n"
        "int main() {}\n"
        "```\n\n"
        "<div>raw html</div>\n";
    std::string error;
    const auto document = litemark::MarkdownParser::Parse(markdown, &error);
    Check(document != nullptr, "required Markdown sample should parse");
    if (!document) {
        return;
    }

    const auto* heading = FindBlock(*document, litemark::BlockKind::Heading);
    Check(heading != nullptr, "heading block missing");
    if (heading != nullptr) {
        Check(heading->headingLevel == 1, "heading level mismatch");
        Check(ContainsText(*heading, "Heading"), "heading text missing");
    }

    bool foundText = false;
    for (const auto& block : document->blocks) {
        if (block.kind != litemark::BlockKind::Text) {
            continue;
        }
        foundText = foundText ||
                    (HasStyle(block, "bold", true, false, false, false) &&
                     HasStyle(block, "italic", false, true, false, false) &&
                     HasStyle(block, "deleted", false, false, true, false) &&
                     HasStyle(block, "code", false, false, false, true) &&
                     HasStyle(block, "link", false, false, false, false, true));
        Check(block.quoteDepth >= 0, "quote depth should not be negative");
    }
    Check(foundText, "required inline styles or link missing");

    bool foundQuote = false;
    bool foundBullet = false;
    bool foundOrderedStart = false;
    bool foundCheckedTask = false;
    bool foundUncheckedTask = false;
    for (const auto& block : document->blocks) {
        foundQuote = foundQuote || block.quoteDepth > 0;
        foundBullet = foundBullet ||
                      (block.listMarker && !block.listOrdered && !block.task);
        foundOrderedStart =
            foundOrderedStart ||
            (block.listMarker && block.listOrdered && block.listIndex == 3);
        foundCheckedTask =
            foundCheckedTask ||
            (block.listMarker && block.task && block.taskChecked);
        foundUncheckedTask =
            foundUncheckedTask ||
            (block.listMarker && block.task && !block.taskChecked);
    }
    Check(foundQuote, "quoted block missing");
    Check(foundBullet, "unordered list marker missing");
    Check(foundOrderedStart, "ordered list should preserve its start index");
    Check(foundCheckedTask, "checked task item missing");
    Check(foundUncheckedTask, "unchecked task item missing");

    const auto* rule = FindBlock(*document, litemark::BlockKind::Rule);
    Check(rule != nullptr, "horizontal rule missing");

    const auto* code = FindBlock(*document, litemark::BlockKind::Code);
    Check(code != nullptr, "fenced code block missing");
    if (code != nullptr) {
        Check(code->language == "cpp", "fenced code language missing");
        Check(ContainsText(*code, "int main"), "fenced code content missing");
    }

    const auto* html = FindBlock(*document, litemark::BlockKind::Html);
    Check(html != nullptr, "raw HTML should be represented as a raw block");
    if (html != nullptr) {
        Check(ContainsText(*html, "raw html"),
              "raw HTML must be preserved instead of silently discarded");
    }

    for (std::size_t i = 0; i < document->blocks.size(); ++i) {
        Check(document->blocks[i].id == i + 1,
              "block ids should be stable, unique, and sequential");
    }
}

void TestTableCellsPreserveInlineStyles() {
    const auto document = litemark::MarkdownParser::Parse(
        "| Name | Value |\n"
        "| --- | --- |\n"
        "| **A** | `one` |\n"
        "| B | two |\n");
    Check(document != nullptr, "table sample should parse");
    if (!document) {
        return;
    }
    const auto* table = FindBlock(*document, litemark::BlockKind::Table);
    Check(table != nullptr, "table block missing");
    if (table == nullptr) {
        return;
    }
    Check(table->tableRows.size() == 3, "table should include header and rows");
    Check(table->tableRows[0].size() == 2, "table column count mismatch");
    Check(table->tableRows[2].size() == 2, "table row should have two cells");
    Check(!table->tableRows[1][0].spans.empty() &&
              table->tableRows[1][0].spans.front().bold,
          "bold table cell style missing");
    Check(!table->tableRows[1][1].spans.empty() &&
              table->tableRows[1][1].spans.front().code,
          "inline code table cell style missing");
}

void TestImagesSplitTextAndPreserveAltAndPath() {
    const auto document = litemark::MarkdownParser::Parse(
        "Before ![diagram](images/diagram.png) after.\n");
    Check(document != nullptr, "image sample should parse");
    if (!document) {
        return;
    }
    Check(document->blocks.size() == 3,
          "paragraph with a single image should split into three blocks");
    if (document->blocks.size() != 3) {
        return;
    }
    Check(document->blocks[0].kind == litemark::BlockKind::Text &&
              ContainsText(document->blocks[0], "Before"),
          "text before image missing");
    Check(document->blocks[1].kind == litemark::BlockKind::Image,
          "image block missing");
    Check(document->blocks[1].imageAlt == "diagram",
          "image alt text missing");
    Check(document->blocks[1].imagePath == "images/diagram.png",
          "image path missing");
    Check(document->blocks[2].kind == litemark::BlockKind::Text &&
              ContainsText(document->blocks[2], "after"),
          "text after image missing");
}

void TestMermaidAndFormulaFallbacksRemainAvailable() {
    const std::string source =
        "```mermaid\n"
        "flowchart RL\n"
        "A[Start] -->|next| B[End]\n"
        "```\n\n"
        "Formula: $E = mc^2$\n";
    const auto document = litemark::MarkdownParser::Parse(source);
    Check(document != nullptr, "Mermaid and formula sample should parse");
    if (!document) {
        return;
    }

    const auto* flow = FindBlock(*document, litemark::BlockKind::Flowchart);
    Check(flow != nullptr, "Mermaid code fence should become a flowchart block");
    if (flow != nullptr) {
        Check(flow->flowSource.find("flowchart RL") != std::string::npos,
              "flowchart source must be retained for fallback");
        const auto parsed = litemark::ParseMermaidFlowchart(flow->flowSource);
        Check(parsed.ok, "supported Mermaid source should parse");
    }

    bool formulaPreserved = false;
    for (const auto& block : document->blocks) {
        if (block.kind == litemark::BlockKind::Text) {
            formulaPreserved =
                formulaPreserved || ContainsText(block, "$E = mc^2$");
        }
    }
    Check(formulaPreserved, "unsupported formula source should remain visible");

    const auto unsupported = litemark::ParseMermaidFlowchart(
        "flowchart TD\n"
        "A --> B\n"
        "style A fill:#fff\n");
    Check(!unsupported.ok,
          "unsupported Mermaid directives should request raw-source fallback");
}

void TestEmptyInputAndCancellation() {
    std::string error;
    const auto empty = litemark::MarkdownParser::Parse("", &error);
    Check(empty != nullptr, "empty Markdown should produce an empty document");
    Check(empty && empty->blocks.empty(), "empty Markdown should have no blocks");

    std::atomic_bool cancelled{true};
    error.clear();
    const auto result =
        litemark::MarkdownParser::Parse("# Cancelled\n", &error, &cancelled);
    Check(result == nullptr, "pre-cancelled parse should stop");
    Check(error.find("cancelled") != std::string::npos,
          "cancelled parse should report cancellation");
}

}  // namespace

int main() {
    TestRequiredMarkdownBlocksAndStyles();
    TestTableCellsPreserveInlineStyles();
    TestImagesSplitTextAndPreserveAltAndPath();
    TestMermaidAndFormulaFallbacksRemainAvailable();
    TestEmptyInputAndCancellation();
    if (failures != 0) {
        std::cerr << "markdown_requirements_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "markdown_requirements_tests: 5 cases passed\n";
    return EXIT_SUCCESS;
}
