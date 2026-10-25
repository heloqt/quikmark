#include "platform/IpcServer.h"

#include <wx/arrstr.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <string>
#include <thread>

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\liteMark.SingleInstance.Pipe";
constexpr DWORD kPipeBufferSize = 64 * 1024;

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

}  // namespace

wxDEFINE_EVENT(LITEMARK_EVT_OPEN_PATHS, wxThreadEvent);

namespace litemark {

bool SendOpenRequestToPrimary(const std::vector<wxString>& paths,
                              int timeoutMs) {
    const std::string payload = ToUtf8Payload(paths);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const BOOL ok = WriteFile(pipe, payload.data(),
                                      static_cast<DWORD>(payload.size()),
                                      &written, nullptr);
            FlushFileBuffers(pipe);
            CloseHandle(pipe);
            return ok != FALSE &&
                   written == static_cast<DWORD>(payload.size());
        }

        if (GetLastError() != ERROR_PIPE_BUSY &&
            GetLastError() != ERROR_FILE_NOT_FOUND) {
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
    while (!stopping_.load(std::memory_order_relaxed)) {
        HANDLE pipe = CreateNamedPipeW(
            kPipeName,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            kPipeBufferSize,
            kPipeBufferSize,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr);
        const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
        if (connectError == ERROR_PIPE_CONNECTED) {
            // A secondary instance connected between CreateNamedPipe and
            // ConnectNamedPipe.
        } else if (!connected && connectError != ERROR_NO_DATA) {
            CloseHandle(pipe);
            continue;
        }

        if (stopping_.load(std::memory_order_relaxed)) {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            break;
        }

        std::string payload;
        std::vector<char> buffer(kPipeBufferSize);
        DWORD bytesRead = 0;
        if (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                     &bytesRead, nullptr) &&
            bytesRead > 0) {
            payload.append(buffer.data(), bytesRead);
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);

        if (!stopping_.load(std::memory_order_relaxed) && !payload.empty()) {
            Dispatch(payload);
        }
    }
}

void IpcServer::Wake() {
    for (int attempt = 0; attempt < 40; ++attempt) {
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            const char end = '\n';
            DWORD written = 0;
            WriteFile(pipe, &end, 1, &written, nullptr);
            FlushFileBuffers(pipe);
            CloseHandle(pipe);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void IpcServer::Dispatch(const std::string& payload) {
    const wxString text = wxString::FromUTF8(payload.data(), payload.size());
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
