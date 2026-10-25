#include "platform/ImageDownloader.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMaxImageBytes = 64ULL * 1024 * 1024;

struct InternetHandleDeleter {
    void operator()(void* handle) const {
        if (handle != nullptr) {
            WinHttpCloseHandle(static_cast<HINTERNET>(handle));
        }
    }
};

using InternetHandle = std::unique_ptr<void, InternetHandleDeleter>;

void SetError(wxString* error, const wchar_t* message) {
    if (error != nullptr) {
        *error = message;
    }
}

}  // namespace

namespace litemark {

bool DownloadImageUrl(const wxString& url, std::vector<std::uint8_t>* bytes,
                      wxString* error) {
    if (bytes == nullptr || url.empty()) {
        SetError(error, L"图片 URL 为空");
        return false;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.wc_str(), static_cast<DWORD>(url.length()), 0,
                         &components)) {
        SetError(error, L"无法解析图片 URL");
        return false;
    }

    const bool secure =
        components.nScheme == INTERNET_SCHEME_HTTPS;
    if (!secure && components.nScheme != INTERNET_SCHEME_HTTP) {
        SetError(error, L"只支持 HTTP/HTTPS 图片");
        return false;
    }

    InternetHandle session(WinHttpOpen(
        L"liteMark/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        SetError(error, L"无法初始化网络连接");
        return false;
    }
    WinHttpSetTimeouts(session.get(), 10000, 10000, 15000, 30000);

    const std::wstring host(components.lpszHostName,
                            components.dwHostNameLength);
    InternetHandle connection(WinHttpConnect(
        session.get(), host.c_str(), components.nPort, 0));
    if (!connection) {
        SetError(error, L"无法连接图片服务器");
        return false;
    }

    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo != nullptr &&
        components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo,
                    components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }

    InternetHandle request(WinHttpOpenRequest(
        connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        SetError(error, L"无法创建图片下载请求");
        return false;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirectPolicy, sizeof(redirectPolicy));

    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        SetError(error, L"图片下载失败");
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
            WINHTTP_NO_HEADER_INDEX) ||
        statusCode < 200 || statusCode >= 300) {
        SetError(error, L"图片服务器返回错误");
        return false;
    }

    DWORD64 contentLength = 0;
    DWORD contentLengthSize = sizeof(contentLength);
    if (WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
            WINHTTP_HEADER_NAME_BY_INDEX, &contentLength,
            &contentLengthSize, WINHTTP_NO_HEADER_INDEX) &&
        contentLength > kMaxImageBytes) {
        SetError(error, L"网络图片超过 64 MiB 限制");
        return false;
    }

    bytes->clear();
    if (contentLength > 0) {
        bytes->reserve(static_cast<std::size_t>(contentLength));
    }

    std::vector<std::uint8_t> buffer(16 * 1024);
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            SetError(error, L"读取网络图片失败");
            return false;
        }
        if (available == 0) {
            break;
        }

        const DWORD toRead =
            std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), toRead, &read)) {
            SetError(error, L"读取网络图片失败");
            return false;
        }
        if (read == 0) {
            break;
        }
        if (bytes->size() + read > kMaxImageBytes) {
            SetError(error, L"网络图片超过 64 MiB 限制");
            return false;
        }
        bytes->insert(bytes->end(), buffer.begin(),
                      buffer.begin() + static_cast<std::ptrdiff_t>(read));
    }

    if (bytes->empty()) {
        SetError(error, L"网络图片内容为空");
        return false;
    }
    return true;
}

}  // namespace litemark
