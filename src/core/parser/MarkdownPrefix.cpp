#include "core/parser/MarkdownPrefix.h"

#include <algorithm>

namespace {

std::size_t LeadingSpaces(std::string_view line) {
    std::size_t count = 0;
    while (count < line.size() && line[count] == ' ') {
        ++count;
    }
    return count;
}

bool IsFenceLine(std::string_view line, std::size_t* fenceLength,
                 char* fenceCharacter, bool closing) {
    const std::size_t indent = LeadingSpaces(line);
    if (indent > 3 || indent >= line.size()) {
        return false;
    }
    const char mark = line[indent];
    if (mark != '`' && mark != '~') {
        return false;
    }

    std::size_t length = 0;
    while (indent + length < line.size() &&
           line[indent + length] == mark) {
        ++length;
    }
    if (length < 3) {
        return false;
    }
    if (closing) {
        for (std::size_t index = indent + length;
             index < line.size(); ++index) {
            if (line[index] != ' ' && line[index] != '\t') {
                return false;
            }
        }
    }

    *fenceLength = length;
    *fenceCharacter = mark;
    return true;
}

bool IsTopLevelHeading(std::string_view line) {
    if (line.empty() || line[0] != '#') {
        return false;
    }
    std::size_t level = 0;
    while (level < line.size() && line[level] == '#') {
        ++level;
    }
    if (level < 1 || level > 6) {
        return false;
    }
    return level == line.size() || line[level] == ' ' ||
           line[level] == '\t';
}

bool IsBlank(std::string_view line) {
    return std::all_of(line.begin(), line.end(), [](char value) {
        return value == ' ' || value == '\t' || value == '\r';
    });
}

}  // namespace

namespace litemark {

std::size_t FindSafeMarkdownPrefixBoundary(
    std::string_view text,
    std::size_t limit) {
    limit = std::min(limit, text.size());
    if (limit == 0) {
        return 0;
    }

    bool inFence = false;
    char fenceCharacter = 0;
    std::size_t fenceLength = 0;
    std::size_t best = 0;
    std::size_t offset = 0;

    while (offset < limit) {
        const std::size_t newline = text.find('\n', offset);
        if (newline == std::string_view::npos || newline >= limit) {
            break;
        }
        const std::string_view line = text.substr(offset, newline - offset);
        const std::size_t nextOffset = newline + 1;

        std::size_t candidateLength = 0;
        char candidateCharacter = 0;
        if (inFence) {
            if (IsFenceLine(line, &candidateLength, &candidateCharacter,
                            true) &&
                candidateCharacter == fenceCharacter &&
                candidateLength >= fenceLength) {
                inFence = false;
                best = nextOffset;
            }
        } else if (IsFenceLine(line, &candidateLength,
                               &candidateCharacter, false)) {
            inFence = true;
            fenceCharacter = candidateCharacter;
            fenceLength = candidateLength;
        } else if (IsTopLevelHeading(line)) {
            if (offset > 0) {
                best = offset;
            }
        } else if (IsBlank(line)) {
            best = nextOffset;
        }

        offset = nextOffset;
    }

    constexpr std::size_t kMinimumUsefulPrefix = 8 * 1024;
    return best >= kMinimumUsefulPrefix && best < text.size() ? best : 0;
}

}  // namespace litemark
