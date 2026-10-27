#pragma once

#include "core/model/Block.h"

#include <cstddef>
#include <optional>
#include <string>

namespace litemark {

struct TableBlockRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct TableClipboardData {
    std::string plainText;
    std::string html;
};

std::optional<TableBlockRange> TableRangeForBlock(
    const MarkdownDocument& document, std::size_t blockIndex);

std::optional<TableClipboardData> BuildTableClipboardData(
    const MarkdownDocument& document, std::size_t blockIndex);

}  // namespace litemark
