#include "platform/IpcServer.h"

#include <wx/arrstr.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kMaxPayloadBytes = 1024 * 1024;

std::string SocketPath() {
    return "/tmp/quikmark-" + std::to_string(getuid()) + ".sock";
}

std::string ToUtf8Payload(const std::vector<wxString>& paths) {
    std::string payload;
    for (const auto& path : paths) {
        const wxScopedCharBuffer utf8 = path.ToUTF8();
        payload.append(utf8.data(), utf8.length());
        payload.push_back('\n');
    }
    if (payload.empty()) {
        payload.push_back('\n');
    }
    return payload;
}

void CloseSocket(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

}  // namespace

wxDEFINE_EVENT(LITEMARK_EVT_OPEN_PATHS, wxThreadEvent);

namespace litemark {

bool SendOpenRequestToPrimary(const std::vector<wxString>& paths,
                              int timeoutMs) {
    const std::string path = SocketPath();
    const std::string payload = ToUtf8Payload(paths);
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeoutMs);

    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(),
                std::min(path.size(), sizeof(address.sun_path) - 1));

    while (std::chrono::steady_clock::now() < deadline) {
        const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            return false;
        }
        if (connect(fd, reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)) == 0) {
            std::size_t written = 0;
            while (written < payload.size()) {
                const ssize_t count =
                    send(fd, payload.data() + written,
                         payload.size() - written, MSG_NOSIGNAL);
                if (count <= 0) {
                    CloseSocket(fd);
                    return false;
                }
                written += static_cast<std::size_t>(count);
            }
            CloseSocket(fd);
            return true;
        }

        const int error = errno;
        CloseSocket(fd);
        if (error != ENOENT && error != ECONNREFUSED) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return false;
}

IpcServer::IpcServer(wxEvtHandler* target)
    : target_(target), thread_(&IpcServer::Run, this) {}

IpcServer::~IpcServer() {
    stopping_.store(true);
    Wake();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void IpcServer::Run() {
    const std::string path = SocketPath();
    struct stat existing {};
    if (lstat(path.c_str(), &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode) || existing.st_uid != getuid()) {
            return;
        }
        unlink(path.c_str());
    }

    const int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0) {
        return;
    }

    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(),
                std::min(path.size(), sizeof(address.sun_path) - 1));
    if (bind(listener, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0 ||
        chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0 ||
        listen(listener, 4) != 0) {
        CloseSocket(listener);
        unlink(path.c_str());
        return;
    }

    while (!stopping_.load(std::memory_order_relaxed)) {
        pollfd descriptor{listener, POLLIN, 0};
        const int ready = poll(&descriptor, 1, 200);
        if (ready <= 0) {
            continue;
        }

        const int client = accept(listener, nullptr, nullptr);
        if (client < 0) {
            continue;
        }

        std::string payload;
        char buffer[4096];
        while (payload.size() < kMaxPayloadBytes) {
            const ssize_t count =
                read(client, buffer, sizeof(buffer));
            if (count > 0) {
                payload.append(buffer, static_cast<std::size_t>(count));
                continue;
            }
            break;
        }
        CloseSocket(client);
        if (!stopping_.load(std::memory_order_relaxed) &&
            !payload.empty()) {
            Dispatch(payload);
        }
    }

    CloseSocket(listener);
    unlink(path.c_str());
}

void IpcServer::Wake() {
    SendOpenRequestToPrimary({}, 100);
}

void IpcServer::Dispatch(const std::string& payload) {
    const wxString text =
        wxString::FromUTF8(payload.data(), payload.size());
    wxArrayString paths = wxSplit(text, '\n', '\0');
    for (int i = static_cast<int>(paths.size()) - 1; i >= 0; --i) {
        if (paths[i].empty()) {
            paths.RemoveAt(static_cast<std::size_t>(i));
        }
    }

    auto* event = new wxThreadEvent(LITEMARK_EVT_OPEN_PATHS);
    event->SetPayload(paths);
    wxQueueEvent(target_, event);
}

}  // namespace litemark
