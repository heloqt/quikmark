#include "ui/StatusIcons.h"

#include <wx/string.h>

#include <cstdint>
#include <iostream>

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "status_icon_tests: " << message << '\n';
    }
    return condition;
}

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

}  // namespace

int main() {
    using litemark::detail::StatusIconId;
    const auto runs = litemark::detail::SplitStatusIcons(
        U8("A✅️B⏸C↘"));
    if (!Check(litemark::detail::ContainsStatusIcon(U8("✅")),
               "check mark was not detected") ||
        !Check(litemark::detail::ContainsStatusIcon(U8("⏸️")),
               "emoji-style pause was not detected") ||
        !Check(litemark::detail::ContainsStatusIcon(U8("⏸")),
               "text-style pause was not detected") ||
        !Check(litemark::detail::ContainsStatusIcon(U8("✔️")),
               "variation-selector check was not detected") ||
        !Check(!litemark::detail::ContainsStatusIcon(
                   U8("普通文本 • →")),
               "plain text was treated as a status icon") ||
        !Check(runs.size() == 6, "status icon runs were not split") ||
        !Check(runs[1].icon && runs[1].id == StatusIconId::Check,
               "check mark run is incorrect") ||
        !Check(runs[3].icon && runs[3].id == StatusIconId::Pause,
               "pause run is incorrect") ||
        !Check(runs[5].icon &&
                   runs[5].id == StatusIconId::DownRightArrow,
               "arrow run is incorrect")) {
        return 1;
    }

    const int iconCount = static_cast<int>(StatusIconId::Count);
    if (!Check(iconCount == 36, "unexpected embedded icon count")) {
        return 1;
    }
    for (int index = 0; index < iconCount; ++index) {
        litemark::detail::StatusIconAsset asset;
        if (!Check(litemark::detail::GetStatusIconAsset(
                       static_cast<StatusIconId>(index), &asset),
                   "missing embedded status icon") ||
            !Check(asset.bytes != nullptr && asset.size >= 8,
                   "empty embedded status icon") ||
            !Check(asset.bytes[0] == 0x89 && asset.bytes[1] == 'P' &&
                       asset.bytes[2] == 'N' &&
                       asset.bytes[3] == 'G',
                   "status icon is not PNG")) {
            std::cerr << "status icon index=" << index
                      << " size=" << asset.size << '\n';
            return 1;
        }
    }

    std::cout << "status_icon_tests: passed\n";
    return 0;
}
