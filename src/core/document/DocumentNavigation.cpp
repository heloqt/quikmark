#include "core/document/DocumentNavigation.h"

#include "core/document/DocumentText.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace {

int HexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

std::string DecodePercentEscapes(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '%' && index + 2 < input.size()) {
            const int high = HexDigit(input[index + 1]);
            const int low = HexDigit(input[index + 2]);
            if (high >= 0 && low >= 0) {
                output.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        output.push_back(input[index]);
    }
    return output;
}

}  // namespace

namespace litemark {

std::string NormalizeHeadingSlug(std::string_view text) {
    const std::string decoded = DecodePercentEscapes(text);
    std::string slug;
    slug.reserve(decoded.size());
    bool previousDash = false;
    for (unsigned char value : decoded) {
        if (std::isalnum(value)) {
            slug.push_back(static_cast<char>(std::tolower(value)));
            previousDash = false;
        } else if (value == '-' || value == '_') {
            if (!slug.empty()) {
                slug.push_back(static_cast<char>(value));
                previousDash = value == '-';
            }
        } else if (std::isspace(value)) {
            if (!slug.empty() && !previousDash) {
                slug.push_back('-');
                previousDash = true;
            }
        } else if (value >= 0x80U) {
            slug.push_back(static_cast<char>(value));
            previousDash = false;
        }
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug.empty() ? "section" : slug;
}

std::vector<HeadingAnchor> BuildHeadingAnchors(
    const MarkdownDocument& document) {
    std::vector<HeadingAnchor> anchors;
    std::map<std::string, int> used;
    for (std::size_t index = 0; index < document.blocks.size(); ++index) {
        const Block& block = document.blocks[index];
        if (block.kind != BlockKind::Heading) {
            continue;
        }
        const std::string text = BlockPlainText(block);
        const std::string base = NormalizeHeadingSlug(text);
        const int occurrence = used[base]++;
        const std::string slug =
            occurrence == 0 ? base : base + "-" + std::to_string(occurrence);
        anchors.push_back(
            HeadingAnchor{index, block.headingLevel, text, slug});
    }
    return anchors;
}

std::optional<std::size_t> FindHeadingBlock(
    const std::vector<HeadingAnchor>& anchors, std::string_view reference) {
    if (!reference.empty() && reference.front() == '#') {
        reference.remove_prefix(1);
    }
    const std::string slug = NormalizeHeadingSlug(reference);
    for (const auto& anchor : anchors) {
        if (anchor.slug == slug) {
            return anchor.blockIndex;
        }
    }
    return std::nullopt;
}

}  // namespace litemark
