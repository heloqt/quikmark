#include "platform/PerformanceTrace.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

std::uint64_t FileTimeValue(const FILETIME& time) {
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

}  // namespace

namespace litemark {

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

}  // namespace litemark
