#include "core/flow/Flowchart.h"
#include "core/parser/MarkdownParser.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool HasKind(const litemark::MarkdownDocument& document,
             litemark::BlockKind kind) {
    for (const auto& block : document.blocks) {
        if (block.kind == kind) {
            return true;
        }
    }
    return false;
}

bool HasBoldText(const litemark::MarkdownDocument& document) {
    for (const auto& block : document.blocks) {
        for (const auto& span : block.spans) {
            if (span.bold && span.text.find("bold") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

bool HasImage(const litemark::MarkdownDocument& document) {
    for (const auto& block : document.blocks) {
        if (block.kind == litemark::BlockKind::Image &&
            block.imagePath.find("diagram.png") != std::string::npos) {
            return true;
        }
        for (const auto& span : block.spans) {
            if (span.image) {
                return true;
            }
        }
    }
    return false;
}

int Fail(const char* message) {
    std::cerr << "parser_smoke: " << message << '\n';
    return EXIT_FAILURE;
}

}  // namespace

int main() {
    const std::string markdown =
        "# Title\n\n"
        "Paragraph with **bold**, *italic*, `code`, ~~deleted~~ and "
        "[link](https://example.com).\n\n"
        "- first\n"
        "- second\n\n"
        "> quoted text\n\n"
        "```cpp\n"
        "int main() {}\n"
        "```\n\n"
        "| Name | Value |\n"
        "| --- | --- |\n"
        "| A | 1 |\n"
        "| B | 2 |\n\n"
        "![diagram](diagram.png)\n\n"
        "```mermaid\n"
        "flowchart TD\n"
        "A[Start] --> B[End]\n"
        "```\n";

    std::string error;
    auto document = litemark::MarkdownParser::Parse(markdown, &error);
    if (!document) {
        return Fail(error.c_str());
    }
    if (!HasKind(*document, litemark::BlockKind::Heading)) {
        return Fail("missing heading block");
    }
    if (!HasKind(*document, litemark::BlockKind::Code)) {
        return Fail("missing code block");
    }
    if (!HasKind(*document, litemark::BlockKind::Table)) {
        return Fail("missing table block");
    }
    if (!HasKind(*document, litemark::BlockKind::Flowchart)) {
        return Fail("missing flowchart block");
    }
    if (!HasBoldText(*document)) {
        return Fail("missing bold inline span");
    }
    if (!HasImage(*document)) {
        return Fail("missing image placeholder span");
    }

    bool hasListMarker = false;
    bool hasQuote = false;
    for (const auto& block : document->blocks) {
        hasListMarker = hasListMarker || block.listMarker;
        hasQuote = hasQuote || block.quoteDepth > 0;
    }
    if (!hasListMarker) {
        return Fail("missing list marker");
    }
    if (!hasQuote) {
        return Fail("missing quote depth");
    }

    const auto flowchart = litemark::ParseMermaidFlowchart(
        "flowchart TD\n"
        "  A[Start] --> B{Ready?}\n"
        "  B -->|Yes| C[Work]\n"
        "  B -->|No| D((Stop))\n"
        "  subgraph group [Work group]\n"
        "  C --> E[Finish]\n"
        "  end\n");
    if (!flowchart.ok) {
        return Fail(flowchart.error.c_str());
    }
    if (flowchart.graph.nodes.size() != 5 ||
        flowchart.graph.edges.size() != 4 ||
        flowchart.graph.clusters.size() != 1 ||
        flowchart.graph.direction != litemark::FlowDirection::TopDown) {
        return Fail("flowchart graph mismatch");
    }
    const auto unsupportedFlowchart = litemark::ParseMermaidFlowchart(
        "flowchart TD\n"
        "  A --> B\n"
        "  style A fill:#fff\n");
    if (unsupportedFlowchart.ok) {
        return Fail("unsupported flowchart syntax was accepted");
    }

    std::cout << "parser_smoke: " << document->blocks.size()
              << " blocks and " << flowchart.graph.nodes.size()
              << " flowchart nodes parsed\n";
    return EXIT_SUCCESS;
}
