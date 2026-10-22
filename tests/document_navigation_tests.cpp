#include "core/document/DocumentNavigation.h"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "document_navigation_tests: " << message << '\n';
        ++failures;
    }
}

litemark::MarkdownDocument HeadingDocument() {
    litemark::MarkdownDocument document;
    const char* headings[] = {
        "Getting Started",
        "API Reference",
        "API Reference",
        "中文 标题",
    };
    for (const char* text : headings) {
        litemark::Block block;
        block.kind = litemark::BlockKind::Heading;
        block.headingLevel = 2;
        block.spans.push_back({text, false, false, false, false, false, ""});
        document.blocks.push_back(block);
        litemark::Block body;
        body.kind = litemark::BlockKind::Text;
        body.spans.push_back({"Body", false, false, false, false, false, ""});
        document.blocks.push_back(body);
    }
    return document;
}

void TestSlugNormalization() {
    Check(litemark::NormalizeHeadingSlug("Getting Started") ==
              "getting-started",
          "ASCII heading slug mismatch");
    Check(litemark::NormalizeHeadingSlug("A  B, C!") == "a-b-c",
          "punctuation and repeated spaces should normalize");
    Check(litemark::NormalizeHeadingSlug("中文 标题") == "中文-标题",
          "Chinese heading text should be retained");
    Check(litemark::NormalizeHeadingSlug("...") == "section",
          "punctuation-only heading should get a stable fallback");
    Check(litemark::NormalizeHeadingSlug("%E4%B8%AD%E6%96%87") == "中文",
          "percent-encoded UTF-8 heading should decode");
}

void TestDuplicateHeadingSuffixes() {
    const auto document = HeadingDocument();
    const auto anchors = litemark::BuildHeadingAnchors(document);
    Check(anchors.size() == 4, "heading anchor count mismatch");
    Check(anchors[0].slug == "getting-started", "first heading slug mismatch");
    Check(anchors[1].slug == "api-reference", "second heading slug mismatch");
    Check(anchors[2].slug == "api-reference-1",
          "duplicate heading should receive a deterministic suffix");
    Check(anchors[1].blockIndex == 2,
          "heading anchor should retain its block index");
    Check(anchors[0].level == 2,
          "heading anchor should retain its heading level");
}

void TestAnchorLookup() {
    const auto document = HeadingDocument();
    const auto anchors = litemark::BuildHeadingAnchors(document);
    Check(litemark::FindHeadingBlock(anchors, "#Getting Started") == 0,
          "anchor lookup should normalize case and spaces");
    Check(litemark::FindHeadingBlock(anchors, "#api-reference-1") == 4,
          "duplicate anchor suffix lookup mismatch");
    Check(litemark::FindHeadingBlock(anchors, "#中文%20标题") == 6,
          "percent-encoded Chinese anchor lookup mismatch");
    Check(!litemark::FindHeadingBlock(anchors, "#missing").has_value(),
          "missing anchor should not resolve");
}

void TestNonHeadingBlocksAreIgnored() {
    const auto document = HeadingDocument();
    const auto anchors = litemark::BuildHeadingAnchors(document);
    for (const auto& anchor : anchors) {
        Check(document.blocks[anchor.blockIndex].kind ==
                  litemark::BlockKind::Heading,
              "anchor index should point exactly to a heading block");
    }
}

}  // namespace

int main() {
    TestSlugNormalization();
    TestDuplicateHeadingSuffixes();
    TestAnchorLookup();
    TestNonHeadingBlocksAreIgnored();
    if (failures != 0) {
        std::cerr << "document_navigation_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "document_navigation_tests: 4 cases passed\n";
    return EXIT_SUCCESS;
}
