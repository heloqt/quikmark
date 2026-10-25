#include "platform/FileAssociation.h"

namespace litemark {

bool IsFileAssociationSupported() {
    return false;
}

bool RegisterLiteMarkFileAssociations(wxString* error) {
    if (error != nullptr) {
        *error = wxString::FromUTF8(
            "当前 Linux 基础版本暂不支持注册文件关联。");
    }
    return false;
}

}  // namespace litemark
