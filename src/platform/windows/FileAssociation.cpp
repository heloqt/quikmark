#include "platform/FileAssociation.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace {

bool SetValue(HKEY root, const wchar_t* subKey, const wchar_t* name,
              const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes =
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG result =
        RegSetValueExW(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

std::wstring ExecutablePath() {
    std::vector<wchar_t> buffer(1024);
    while (true) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
}

}  // namespace

namespace litemark {

bool IsFileAssociationSupported() {
    return true;
}

bool RegisterLiteMarkFileAssociations(wxString* error) {
    const std::wstring executable = ExecutablePath();
    if (executable.empty()) {
        if (error != nullptr) {
            *error = wxString::FromUTF8("无法获取 liteMark 可执行文件路径");
        }
        return false;
    }

    const std::wstring openCommand =
        L"\"" + executable + L"\" \"%1\"";
    const wchar_t* extensions[] = {
        L"Software\\Classes\\.md",
        L"Software\\Classes\\.markdown",
    };

    for (const wchar_t* extension : extensions) {
        if (!SetValue(HKEY_CURRENT_USER, extension, nullptr,
                      L"liteMark.Markdown")) {
            if (error != nullptr) {
                *error = wxString::FromUTF8("写入文件扩展名关联失败");
            }
            return false;
        }
    }

    const std::wstring progId = L"Software\\Classes\\liteMark.Markdown";
    if (!SetValue(HKEY_CURRENT_USER, progId.c_str(), nullptr,
                  L"Markdown Document") ||
        !SetValue(HKEY_CURRENT_USER, (progId + L"\\DefaultIcon").c_str(),
                  nullptr, L"\"" + executable + L"\",0") ||
        !SetValue(HKEY_CURRENT_USER,
                  (progId + L"\\shell\\open\\command").c_str(), nullptr,
                  openCommand)) {
        if (error != nullptr) {
            *error = wxString::FromUTF8("注册 Markdown 打开命令失败");
        }
        return false;
    }

    const std::wstring application =
        L"Software\\Classes\\Applications\\liteMark.exe";
    if (!SetValue(HKEY_CURRENT_USER,
                  (application + L"\\shell\\open\\command").c_str(), nullptr,
                  openCommand) ||
        !SetValue(HKEY_CURRENT_USER,
                  (application + L"\\SupportedTypes\\.md").c_str(), nullptr,
                  L"") ||
        !SetValue(HKEY_CURRENT_USER,
                  (application + L"\\SupportedTypes\\.markdown").c_str(),
                  nullptr, L"")) {
        if (error != nullptr) {
            *error = wxString::FromUTF8("注册“打开方式”失败");
        }
        return false;
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

}  // namespace litemark
