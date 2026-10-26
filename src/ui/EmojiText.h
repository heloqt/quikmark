#pragma once

#include <wx/string.h>

#include <vector>

namespace litemark::detail {

struct EmojiTextRun {
    int begin = 0;
    int end = 0;
    bool emoji = false;
};

bool ContainsEmojiCandidate(const wxString& text);
std::vector<EmojiTextRun> SplitEmojiText(const wxString& text);

}  // namespace litemark::detail
