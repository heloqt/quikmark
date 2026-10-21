#pragma once

#include <cstdint>

namespace litemark {

bool PerformanceTraceEnabled();
std::uint64_t ProcessAgeMicroseconds();
void TraceEvent(const char* event, double microseconds);

}  // namespace litemark
