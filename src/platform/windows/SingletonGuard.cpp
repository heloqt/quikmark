#include "platform/windows/SingletonGuard.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace litemark {

SingletonGuard::SingletonGuard(const wchar_t* name) {
    mutex_ = CreateMutexW(nullptr, FALSE, name);
    primary_ = mutex_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

SingletonGuard::~SingletonGuard() {
    if (mutex_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mutex_));
        mutex_ = nullptr;
    }
}

}  // namespace litemark
