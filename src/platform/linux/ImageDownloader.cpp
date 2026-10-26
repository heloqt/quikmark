#include "platform/ImageDownloader.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMaxImageBytes = 64ULL * 1024 * 1024;

struct DownloadState {
    std::vector<std::uint8_t>* bytes = nullptr;
    bool tooLarge = false;
};

void SetError(wxString* error, const char* message) {
    if (error != nullptr) {
        *error = wxString::FromUTF8(message);
    }
}

bool EnsureCurlInitialized() {
    static std::once_flag once;
    static CURLcode initResult = CURLE_FAILED_INIT;
    std::call_once(once, []() {
        initResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    });
    return initResult == CURLE_OK;
}

std::size_t WriteBody(char* data, std::size_t size, std::size_t count,
                      void* userData) {
    DownloadState* state = static_cast<DownloadState*>(userData);
    const std::size_t bytes = size * count;
    if (bytes == 0) {
        return 0;
    }
    if (state == nullptr || state->bytes == nullptr ||
        state->bytes->size() + bytes > kMaxImageBytes) {
        if (state != nullptr) {
            state->tooLarge = true;
        }
        return 0;
    }
    const auto* begin = reinterpret_cast<const std::uint8_t*>(data);
    state->bytes->insert(state->bytes->end(), begin, begin + bytes);
    return bytes;
}

bool StartsWithHttpScheme(const std::string& url) {
    auto lower = [](char ch) {
        return static_cast<char>(std::tolower(
            static_cast<unsigned char>(ch)));
    };
    if (url.size() < 7) {
        return false;
    }
    std::string prefix(url.substr(0, 8));
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), lower);
    return prefix.rfind("http://", 0) == 0 ||
           prefix.rfind("https://", 0) == 0;
}

}  // namespace

namespace litemark {

bool DownloadImageUrl(const wxString& url, std::vector<std::uint8_t>* bytes,
                      wxString* error) {
    if (bytes == nullptr || url.empty()) {
        SetError(error, "图片 URL 为空");
        return false;
    }
    bytes->clear();
    if (!EnsureCurlInitialized()) {
        SetError(error, "无法初始化网络连接");
        return false;
    }

    const wxScopedCharBuffer urlUtf8 = url.ToUTF8();
    const std::string urlText(urlUtf8.data(), urlUtf8.length());
    if (!StartsWithHttpScheme(urlText)) {
        SetError(error, "只支持 HTTP/HTTPS 图片");
        return false;
    }

    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        SetError(error, "无法创建图片下载请求");
        return false;
    }

    DownloadState state;
    state.bytes = bytes;
    char errorBuffer[CURL_ERROR_SIZE]{};

    curl_easy_setopt(handle, CURLOPT_URL, urlText.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "QuikMark/1.1");
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS,
                     static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS,
                     static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(handle, CURLOPT_MAXFILESIZE_LARGE,
                     static_cast<curl_off_t>(kMaxImageBytes));

    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(handle);

    if (state.tooLarge) {
        bytes->clear();
        SetError(error, "网络图片超过 64 MiB 限制");
        return false;
    }
    if (result != CURLE_OK) {
        bytes->clear();
        if (result == CURLE_FILESIZE_EXCEEDED) {
            SetError(error, "网络图片超过 64 MiB 限制");
        } else {
            SetError(error,
                     errorBuffer[0] != '\0'
                         ? errorBuffer
                         : "图片下载失败");
        }
        return false;
    }
    if (status < 200 || status >= 300) {
        bytes->clear();
        SetError(error, "图片服务器返回错误");
        return false;
    }
    if (bytes->empty()) {
        SetError(error, "网络图片内容为空");
        return false;
    }
    return true;
}

}  // namespace litemark
