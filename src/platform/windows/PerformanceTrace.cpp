#include "platform/windows/PerformanceTrace.h"

#include <wx/string.h>
#include <wx/utils.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fstream>
#include <mutex>
#include <string>

namespace {

std::string TracePath() {
    static std::once_flag once;
    static std::string path;
    std::call_once(once, []() {
        wxString configured;
        if (wxGetEnv("LITEMARK_PERF_TRACE", &configured) &&
            !configured.empty()) {
            const wxScopedCharBuffer utf8 = configured.ToUTF8();
            path.assign(utf8.data(), utf8.length());
        }
    });
    return path;
}

std::uint64_t FileTimeValue(const FILETIME& time) {
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

}  // namespace

namespace litemark {

bool PerformanceTraceEnabled() {
    return !TracePath().empty();
}

std::uint64_t ProcessAgeMicroseconds() {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    FILETIME now{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        return 0;
    }
    GetSystemTimePreciseAsFileTime(&now);
    const std::uint64_t created = FileTimeValue(creation);
    const std::uint64_t current = FileTimeValue(now);
    return current > created ? (current - created) / 10 : 0;
}

void TraceEvent(const char* event, double microseconds) {
    const std::string path = TracePath();
    if (path.empty()) {
        return;
    }
    std::ofstream output(path, std::ios::app);
    output << event << ',' << microseconds << '\n';
}

}  // namespace litemark
