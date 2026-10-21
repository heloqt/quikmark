#pragma once

#include <wx/string.h>

#include <cstdint>
#include <vector>

namespace litemark {

bool DownloadImageUrl(const wxString& url,
                      std::vector<std::uint8_t>* bytes,
                      wxString* error = nullptr);

}  // namespace litemark
