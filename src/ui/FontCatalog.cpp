#include "ui/FontCatalog.h"

#include <wx/fontenum.h>

#include <initializer_list>

namespace {

wxString FirstAvailable(
    std::initializer_list<const char*> candidates) {
    for (const char* candidate : candidates) {
        if (wxFontEnumerator::IsValidFacename(candidate)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace

namespace litemark {

wxString UiFaceName() {
    return FirstAvailable({
        "Microsoft YaHei UI",
        "Segoe UI",
        "Noto Sans CJK SC",
        "Source Han Sans SC",
        "WenQuanYi Micro Hei",
        "Noto Sans",
        "DejaVu Sans",
    });
}

wxString MonoFaceName() {
    return FirstAvailable({
        "Cascadia Mono",
        "Consolas",
        "JetBrains Mono",
        "Noto Sans Mono CJK SC",
        "DejaVu Sans Mono",
    });
}

}  // namespace litemark
