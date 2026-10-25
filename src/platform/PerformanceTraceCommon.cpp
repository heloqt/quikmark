#include "platform/PerformanceTrace.h"

#include <wx/string.h>
#include <wx/utils.h>

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

}  // namespace

namespace litemark {

bool PerformanceTraceEnabled() {
    return !TracePath().empty();
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
