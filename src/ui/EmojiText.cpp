#include "ui/EmojiText.h"

#include <cstdint>

namespace {

std::uint32_t CodePointAt(const wxString& text, int index, int* advance) {
    const std::uint32_t first = text[index].GetValue();
    if (first >= 0xD800 && first <= 0xDBFF &&
        index + 1 < static_cast<int>(text.length())) {
        const std::uint32_t second = text[index + 1].GetValue();
        if (second >= 0xDC00 && second <= 0xDFFF) {
            *advance = 2;
            return 0x10000 + ((first - 0xD800) << 10) +
                   (second - 0xDC00);
        }
    }
    *advance = 1;
    return first;
}

bool IsEmojiRange(std::uint32_t value) {
    return (value >= 0x2300 && value <= 0x23FF) ||
           (value >= 0x2600 && value <= 0x27BF) ||
           (value >= 0x2B00 && value <= 0x2BFF) ||
           (value >= 0x1F000 && value <= 0x1FAFF) ||
           value == 0x200D || value == 0x20E3 ||
           value == 0x3030 || value == 0x303D ||
           value == 0x3297 || value == 0x3299 ||
           (value >= 0xFE00 && value <= 0xFE0F);
}

bool IsRegionalIndicator(std::uint32_t value) {
    return value >= 0x1F1E6 && value <= 0x1F1FF;
}

bool IsEmojiModifier(std::uint32_t value) {
    return (value >= 0x1F3FB && value <= 0x1F3FF) ||
           (value >= 0xFE00 && value <= 0xFE0F) ||
           (value >= 0xE0020 && value <= 0xE007F) ||
           value == 0x20E3;
}

void AdvanceCodePoint(const wxString& text, int* index) {
    int advance = 1;
    CodePointAt(text, *index, &advance);
    *index += advance;
}

int EmojiClusterEnd(const wxString& text, int begin) {
    int index = begin;
    AdvanceCodePoint(text, &index);
    int codePointAdvance = 1;
    const std::uint32_t first =
        CodePointAt(text, begin, &codePointAdvance);
    if (IsRegionalIndicator(first) &&
        index < static_cast<int>(text.length())) {
        const std::uint32_t next =
            CodePointAt(text, index, &codePointAdvance);
        if (IsRegionalIndicator(next)) {
            index += codePointAdvance;
        }
    }

    while (index < static_cast<int>(text.length())) {
        int advance = 1;
        const std::uint32_t value = CodePointAt(text, index, &advance);
        if (IsEmojiModifier(value)) {
            index += advance;
            continue;
        }
        if (value == 0x200D &&
            index + advance < static_cast<int>(text.length())) {
            index += advance;
            const std::uint32_t joined =
                CodePointAt(text, index, &advance);
            index += advance;
            if (!IsEmojiRange(joined) && joined != 0x2640 &&
                joined != 0x2642) {
                break;
            }
            continue;
        }
        break;
    }
    return index;
}

}  // namespace

namespace litemark::detail {

bool ContainsEmojiCandidate(const wxString& text) {
    int index = 0;
    while (index < static_cast<int>(text.length())) {
        int advance = 1;
        if (IsEmojiRange(CodePointAt(text, index, &advance))) {
            return true;
        }
        index += advance;
    }
    return false;
}

std::vector<EmojiTextRun> SplitEmojiText(const wxString& text) {
    std::vector<EmojiTextRun> runs;
    int index = 0;
    while (index < static_cast<int>(text.length())) {
        int advance = 1;
        const std::uint32_t value = CodePointAt(text, index, &advance);
        const bool emoji = IsEmojiRange(value);
        int end = index + advance;
        if (emoji) {
            end = EmojiClusterEnd(text, index);
        } else {
            while (end < static_cast<int>(text.length())) {
                int nextAdvance = 1;
                if (IsEmojiRange(
                        CodePointAt(text, end, &nextAdvance))) {
                    break;
                }
                end += nextAdvance;
            }
        }
        if (!runs.empty() && runs.back().emoji == emoji) {
            runs.back().end = end;
        } else {
            runs.push_back(EmojiTextRun{index, end, emoji});
        }
        index = end;
    }
    return runs;
}

}  // namespace litemark::detail
