#pragma once

namespace litemark {

class SingletonGuard {
public:
    explicit SingletonGuard(const wchar_t* name);
    ~SingletonGuard();

    SingletonGuard(const SingletonGuard&) = delete;
    SingletonGuard& operator=(const SingletonGuard&) = delete;

    bool IsPrimary() const { return primary_; }

private:
    void* mutex_ = nullptr;
    bool primary_ = false;
};

}  // namespace litemark
