#include "platform/ImageDownloader.h"

#include <cstdint>
#include <vector>

namespace litemark {

bool DownloadImageUrl(const wxString&, std::vector<std::uint8_t>* bytes,
                      wxString* error) {
    if (bytes != nullptr) {
        bytes->clear();
    }
    if (error != nullptr) {
        *error = wxString::FromUTF8(
            "当前 Linux 基础版本暂不支持网络图片下载。");
    }
    return false;
}

}  // namespace litemark
