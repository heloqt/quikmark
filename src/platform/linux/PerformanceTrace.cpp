#include "platform/PerformanceTrace.h"

#include <time.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::uint64_t ProcessStartTicks() {
    std::ifstream input("/proc/self/stat");
    std::string line;
    if (!std::getline(input, line)) {
        return 0;
    }
    const std::size_t commandEnd = line.rfind(')');
    if (commandEnd == std::string::npos || commandEnd + 2 >= line.size()) {
        return 0;
    }

    std::istringstream fields(line.substr(commandEnd + 2));
    std::string value;
    for (int index = 0; index <= 19; ++index) {
        if (!(fields >> value)) {
            return 0;
        }
    }
    try {
        return std::stoull(value);
    } catch (...) {
        return 0;
    }
}

}  // namespace

namespace litemark {

std::uint64_t ProcessAgeMicroseconds() {
    const std::uint64_t startTicks = ProcessStartTicks();
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    struct timespec now {};
    if (startTicks == 0 || ticksPerSecond <= 0 ||
        clock_gettime(CLOCK_BOOTTIME, &now) != 0) {
        return 0;
    }

    const double uptime =
        static_cast<double>(now.tv_sec) +
        static_cast<double>(now.tv_nsec) / 1000000000.0;
    const double started =
        static_cast<double>(startTicks) /
        static_cast<double>(ticksPerSecond);
    if (uptime <= started) {
        return 0;
    }
    return static_cast<std::uint64_t>(
        std::llround((uptime - started) * 1000000.0));
}

}  // namespace litemark
