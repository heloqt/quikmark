#include "platform/SingletonGuard.h"

#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <string>

namespace {

std::string LockPath(const char* name) {
    std::string leaf =
        name != nullptr && *name != '\0' ? name : "litemark";
    for (char& ch : leaf) {
        if (ch == '/' || ch == '\\') {
            ch = '_';
        }
    }
    return "/tmp/" + leaf + "-" + std::to_string(getuid()) + ".lock";
}

}  // namespace

namespace litemark {

struct SingletonGuard::Impl {
    int fd = -1;
};

SingletonGuard::SingletonGuard(const char* name) {
    impl_ = std::make_unique<Impl>();
    const std::string path = LockPath(name);
    impl_->fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
                     S_IRUSR | S_IWUSR);
    if (impl_->fd < 0) {
        return;
    }

    struct stat info {};
    if (fstat(impl_->fd, &info) != 0 || info.st_uid != getuid()) {
        close(impl_->fd);
        impl_->fd = -1;
        return;
    }

    if (flock(impl_->fd, LOCK_EX | LOCK_NB) == 0) {
        primary_ = true;
    }
}

SingletonGuard::~SingletonGuard() {
    if (impl_ != nullptr && impl_->fd >= 0) {
        flock(impl_->fd, LOCK_UN);
        close(impl_->fd);
        impl_->fd = -1;
    }
}

}  // namespace litemark
