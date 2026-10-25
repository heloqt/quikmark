#include "platform/ImageDownloader.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

const std::vector<std::uint8_t> kPayload = {
    0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
};

class LocalServer {
public:
    LocalServer() {
        listener_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (listener_ < 0) {
            return;
        }
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listener_, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) != 0 ||
            listen(listener_, 8) != 0) {
            close(listener_);
            listener_ = -1;
            return;
        }
        socklen_t length = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address),
                        &length) != 0) {
            close(listener_);
            listener_ = -1;
            return;
        }
        port_ = ntohs(address.sin_port);
        thread_ = std::thread(&LocalServer::Run, this);
    }

    ~LocalServer() {
        stopping_.store(true);
        if (listener_ >= 0) {
            shutdown(listener_, SHUT_RDWR);
            close(listener_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool IsReady() const { return listener_ >= 0; }
    int port() const { return port_; }

private:
    void SendResponse(int client, const char* header,
                      const std::vector<std::uint8_t>& body) {
        const std::string head =
            std::string(header) +
            "Content-Length: " + std::to_string(body.size()) +
            "\r\nConnection: close\r\n\r\n";
        send(client, head.data(), head.size(), MSG_NOSIGNAL);
        if (!body.empty()) {
            send(client, body.data(), body.size(), MSG_NOSIGNAL);
        }
    }

    void Run() {
        while (!stopping_.load()) {
            const int client = accept(listener_, nullptr, nullptr);
            if (client < 0) {
                break;
            }
            char request[2048]{};
            const ssize_t received =
                recv(client, request, sizeof(request) - 1, 0);
            const std::string text(request, static_cast<std::size_t>(
                                               std::max<ssize_t>(received, 0)));
            if (text.find("GET /ok ") == 0) {
                SendResponse(client,
                             "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n",
                             kPayload);
            } else if (text.find("GET /redirect ") == 0) {
                SendResponse(
                    client,
                    "HTTP/1.1 302 Found\r\nLocation: /ok\r\n",
                    {});
            } else {
                SendResponse(client, "HTTP/1.1 404 Not Found\r\n", {});
            }
            shutdown(client, SHUT_RDWR);
            close(client);
        }
    }

    int listener_ = -1;
    int port_ = 0;
    std::atomic_bool stopping_ = false;
    std::thread thread_;
};

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "linux_image_downloader_tests: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main() {
    setenv("NO_PROXY", "127.0.0.1,localhost", 1);
    LocalServer server;
    if (!Expect(server.IsReady(), "local HTTP server did not start")) {
        return 1;
    }

    const std::string base =
        "http://127.0.0.1:" + std::to_string(server.port());
    std::vector<std::uint8_t> bytes;
    wxString error;

    if (!Expect(litemark::DownloadImageUrl(
                    wxString::FromUTF8(base + "/ok"), &bytes, &error),
                "HTTP success download failed") ||
        !Expect(bytes == kPayload, "downloaded body mismatch") ||
        !Expect(litemark::DownloadImageUrl(
                    wxString::FromUTF8(base + "/redirect"), &bytes, &error),
                "HTTP redirect download failed") ||
        !Expect(bytes == kPayload, "redirected body mismatch") ||
        !Expect(!litemark::DownloadImageUrl(
                    wxString::FromUTF8(base + "/missing"), &bytes, &error),
                "HTTP 404 should fail") ||
        !Expect(!error.empty(), "HTTP 404 should provide an error") ||
        !Expect(!litemark::DownloadImageUrl("file:///etc/passwd", &bytes,
                                           &error),
                "non-HTTP URL should fail")) {
        return 1;
    }

    std::cout << "linux_image_downloader_tests: passed\n";
    return 0;
}
