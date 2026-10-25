#pragma once

#include <memory>

namespace litemark {

class SingletonGuard {
public:
    explicit SingletonGuard(const char* name);
    ~SingletonGuard();

    SingletonGuard(const SingletonGuard&) = delete;
    SingletonGuard& operator=(const SingletonGuard&) = delete;

    bool IsPrimary() const { return primary_; }

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
    bool primary_ = false;
};

}  // namespace litemark
