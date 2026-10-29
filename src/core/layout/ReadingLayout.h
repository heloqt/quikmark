#pragma once

#include <algorithm>
#include <cmath>

namespace litemark {

inline constexpr int kReadingPageMarginDip = 34;
inline constexpr int kReadingTextMaxWidthDip = 840;
inline constexpr int kReadingTableMaxWidthDip = 1032;

inline int ScaleReadingDip(int value, double dpiScale,
                           double fontScale = 1.0) {
    return static_cast<int>(
        std::lround(value * dpiScale * fontScale));
}

inline int ReadingTextWidth(int availableWidth, double dpiScale,
                            double fontScale) {
    return std::clamp(
        ScaleReadingDip(kReadingTextMaxWidthDip, dpiScale, fontScale),
        0, std::max(0, availableWidth));
}

inline int ReadingTableWidth(int desiredWidth, int availableWidth,
                             double dpiScale, double fontScale) {
    availableWidth = std::max(0, availableWidth);
    const int minimum = std::min(
        availableWidth,
        ScaleReadingDip(kReadingTextMaxWidthDip, dpiScale, fontScale));
    const int maximum = std::min(
        availableWidth,
        ScaleReadingDip(kReadingTableMaxWidthDip, dpiScale, fontScale));
    return std::clamp(desiredWidth, minimum, maximum);
}

inline int CenteredReadingLeft(int availableWidth, int contentWidth) {
    return std::max(0, (availableWidth - contentWidth) / 2);
}

}  // namespace litemark
