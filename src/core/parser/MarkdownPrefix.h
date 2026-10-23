#pragma once

#include <cstddef>
#include <string_view>

namespace litemark {

inline constexpr std::size_t kMarkdownPrefixBytes = 32 * 1024;

std::size_t FindSafeMarkdownPrefixBoundary(
    std::string_view text,
    std::size_t limit = kMarkdownPrefixBytes);

}  // namespace litemark
