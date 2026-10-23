#pragma once

#include <algorithm>
#include <cmath>

namespace litemark {

struct ListLayoutMetrics {
    int markerOffset = 0;
    int markerGutter = 0;
    int textOffset = 0;
};

inline int ListNumberDigits(int value) {
    int digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

inline ListLayoutMetrics ComputeListLayout(
    int listDepth,
    bool ordered,
    int listIndex,
    double dpiScale,
    double fontScale) {
    if (listDepth <= 0) {
        return {};
    }

    constexpr double kBaseIndentDIP = 4.0;
    constexpr double kLevelIndentDIP = 22.0;
    constexpr double kMarkerGutterDIP = 28.0;
    const double zoom = std::max(0.1, dpiScale * fontScale);
    const double digits =
        static_cast<double>(ListNumberDigits(std::max(1, listIndex)));
    const double orderedGutter =
        ordered ? std::max(kMarkerGutterDIP, 10.0 + digits * 8.0)
                : kMarkerGutterDIP;
    ListLayoutMetrics metrics;
    metrics.markerOffset = static_cast<int>(std::lround(
        (kBaseIndentDIP + (listDepth - 1) * kLevelIndentDIP) * zoom));
    metrics.markerGutter =
        static_cast<int>(std::lround(orderedGutter * zoom));
    metrics.textOffset = metrics.markerOffset + metrics.markerGutter;
    return metrics;
}

}  // namespace litemark
