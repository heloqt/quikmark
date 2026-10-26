#include "ui/StatusIcons.h"

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

bool TryStatusIconAt(const wxString& text, int index,
                     litemark::detail::StatusIconId* id, int* end) {
    int advance = 1;
    const std::uint32_t value = CodePointAt(text, index, &advance);
    if (!litemark::detail::TryStatusIconForCodePoint(value, id)) {
        return false;
    }
    int next = index + advance;
    while (next < static_cast<int>(text.length())) {
        const std::uint32_t modifier = text[next].GetValue();
        if (modifier != 0xFE0E && modifier != 0xFE0F) {
            break;
        }
        ++next;
    }
    *end = next;
    return true;
}

}  // namespace

namespace litemark::detail {

bool ContainsStatusIcon(const wxString& text) {
    int index = 0;
    while (index < static_cast<int>(text.length())) {
        StatusIconId id = StatusIconId::Count;
        int end = index;
        if (TryStatusIconAt(text, index, &id, &end)) {
            return true;
        }
        int advance = 1;
        CodePointAt(text, index, &advance);
        index += advance;
    }
    return false;
}

std::vector<StatusIconRun> SplitStatusIcons(const wxString& text) {
    std::vector<StatusIconRun> runs;
    int index = 0;
    while (index < static_cast<int>(text.length())) {
        StatusIconId id = StatusIconId::Count;
        int end = index;
        const bool icon = TryStatusIconAt(text, index, &id, &end);
        if (!icon) {
            int advance = 1;
            CodePointAt(text, index, &advance);
            end = index + advance;
            while (end < static_cast<int>(text.length())) {
                StatusIconId ignored = StatusIconId::Count;
                int iconEnd = end;
                if (TryStatusIconAt(text, end, &ignored, &iconEnd)) {
                    break;
                }
                CodePointAt(text, end, &advance);
                end += advance;
            }
        }
        if (!icon && !runs.empty() && !runs.back().icon) {
            runs.back().end = end;
        } else {
            runs.push_back(StatusIconRun{index, end, icon, id});
        }
        index = end;
    }
    return runs;
}

}  // namespace litemark::detail
