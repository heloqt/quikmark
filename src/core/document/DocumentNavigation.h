#pragma once

#include "core/model/Block.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace litemark {

struct HeadingAnchor {
    std::size_t blockIndex = 0;
    std::string text;
    std::string slug;
};

std::string NormalizeHeadingSlug(std::string_view text);

std::vector<HeadingAnchor> BuildHeadingAnchors(
    const MarkdownDocument& document);

std::optional<std::size_t> FindHeadingBlock(
    const std::vector<HeadingAnchor>& anchors, std::string_view reference);

}  // namespace litemark
