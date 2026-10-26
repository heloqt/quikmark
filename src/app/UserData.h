#pragma once

#include <wx/string.h>

namespace litemark::app {

wxString ConfigRoot();
wxString ConfigDirectoryName();
wxString ConfigDirectory();
wxString ConfigFilePath(const wxString& name);

bool MigrateLegacyUserData(const wxString& legacyDirectory,
                           const wxString& destinationDirectory);
void MigrateLegacyUserData();

}  // namespace litemark::app
