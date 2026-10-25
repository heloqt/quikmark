#include "platform/SingletonGuard.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace litemark {

struct SingletonGuard::Impl {
    void* mutex = nullptr;
};

SingletonGuard::SingletonGuard(const char* name) {
    std::wstring wideName(L"Local\\");
    if (name != nullptr) {
        while (*name != '\0') {
            wideName.push_back(
                static_cast<wchar_t>(static_cast<unsigned char>(*name++)));
        }
    }
    impl_ = std::make_unique<Impl>();
    impl_->mutex = CreateMutexW(
        nullptr, FALSE, wideName.empty() ? nullptr : wideName.c_str());
    primary_ =
        impl_->mutex != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

SingletonGuard::~SingletonGuard() {
    if (impl_ != nullptr && impl_->mutex != nullptr) {
        CloseHandle(static_cast<HANDLE>(impl_->mutex));
        impl_->mutex = nullptr;
    }
}

}  // namespace litemark
