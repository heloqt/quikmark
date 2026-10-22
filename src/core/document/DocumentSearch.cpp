#include "core/document/DocumentSearch.h"

#include "core/document/DocumentText.h"

#include <algorithm>
#include <cctype>

namespace {

std::string LowerAscii(std::string_view text) {
    std::string result(text);
    for (char& value : result) {
        const unsigned char ch = static_cast<unsigned char>(value);
        if (ch < 0x80U) {
            value = static_cast<char>(std::tolower(ch));
        }
    }
    return result;
}

bool IsCancelled(const std::atomic_bool* cancelled) {
    return cancelled != nullptr &&
           cancelled->load(std::memory_order_relaxed);
}

}  // namespace

namespace litemark {

DocumentSearchResult FindDocumentText(
    const MarkdownDocument& document,
    std::string_view query,
    std::size_t maxResults,
    const std::atomic_bool* cancelled) {
    DocumentSearchResult result;
    if (query.empty() || maxResults == 0 || IsCancelled(cancelled)) {
        return result;
    }

    const std::string needle = LowerAscii(query);
    for (std::size_t blockIndex = 0;
         blockIndex < document.blocks.size(); ++blockIndex) {
        if (IsCancelled(cancelled)) {
            result.matches.clear();
            result.truncated = false;
            return result;
        }
        const std::string text = BlockPlainText(document.blocks[blockIndex]);
        const std::string folded = LowerAscii(text);
        std::size_t offset = 0;
        while (offset + needle.size() <= folded.size()) {
            if (IsCancelled(cancelled)) {
                result.matches.clear();
                result.truncated = false;
                return result;
            }
            const std::size_t found = folded.find(needle, offset);
            if (found == std::string::npos) {
                break;
            }
            if (result.matches.size() >= maxResults) {
                result.truncated = true;
                return result;
            }
            result.matches.push_back(
                SearchMatch{blockIndex, found, needle.size()});
            offset = found + std::max<std::size_t>(1, needle.size());
        }
    }
    return result;
}

}  // namespace litemark
