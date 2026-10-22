#pragma once

#include "core/model/Block.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace litemark {

struct SearchMatch {
    std::size_t blockIndex = 0;
    std::size_t utf8Offset = 0;
    std::size_t utf8Length = 0;
};

struct DocumentSearchResult {
    std::vector<SearchMatch> matches;
    bool truncated = false;
};

DocumentSearchResult FindDocumentText(
    const MarkdownDocument& document,
    std::string_view query,
    std::size_t maxResults = 10000,
    const std::atomic_bool* cancelled = nullptr);

}  // namespace litemark
