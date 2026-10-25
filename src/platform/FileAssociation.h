#pragma once

#include <wx/string.h>

namespace litemark {

bool IsFileAssociationSupported();
bool RegisterLiteMarkFileAssociations(wxString* error = nullptr);

}  // namespace litemark
