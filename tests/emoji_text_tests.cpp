#include "ui/EmojiText.h"

#include <wx/string.h>

#include <iostream>

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "emoji_text_tests: " << message << '\n';
    }
    return condition;
}

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

}  // namespace

int main() {
    if (!Check(litemark::detail::ContainsEmojiCandidate(
                   U8("✅️ ❌️ ⌛️ ⏸️ ⚠️")),
               "common status emoji were not detected") ||
        !Check(litemark::detail::ContainsEmojiCandidate(
                   wxString::FromUTF8("\xF0\x9F\x9F\xA2")),
               "supplementary-plane emoji was not detected") ||
        !Check(!litemark::detail::ContainsEmojiCandidate(
                   U8("普通文本 • →")),
               "plain text was incorrectly treated as emoji") ||
        !Check(
            litemark::detail::SplitEmojiText(U8("A✅️B⏸️C")).size() == 5,
            "emoji text runs were not split correctly")) {
        return 1;
    }

    std::cout << "emoji_text_tests: passed\n";
    return 0;
}
