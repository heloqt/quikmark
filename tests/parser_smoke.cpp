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
        "![diagram](diagram.png)\n";

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

    std::cout << "parser_smoke: " << document->blocks.size()
              << " blocks parsed\n";
    return EXIT_SUCCESS;
}
