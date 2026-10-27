#pragma once

#include <wx/intl.h>
#include <wx/string.h>

#include <string>

namespace litemark {

inline wxString Tr(const char* text) {
    return wxGetTranslation(wxString::FromUTF8(text));
}

inline wxString Tr(const std::string& text) {
    return wxGetTranslation(
        wxString::FromUTF8(text.data(), text.size()));
}

}  // namespace litemark
