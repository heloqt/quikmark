#include "core/layout/ReadingLayout.h"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "reading_layout_tests: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace litemark;

    Check(ReadingTextWidth(1800, 1.0, 1.0) == 840,
          "wide text column should be capped at 840 DIP");
    Check(ReadingTextWidth(700, 1.0, 1.0) == 700,
          "narrow text column should use available width");
    Check(ReadingTextWidth(1800, 1.0, 2.0) == 1680,
          "text column should scale with font zoom");
    Check(CenteredReadingLeft(1800, 840) == 480,
          "text column should be centered");

    Check(ReadingTableWidth(500, 1800, 1.0, 1.0) == 840,
          "small table should use the minimum reading width");
    Check(ReadingTableWidth(900, 1800, 1.0, 1.0) == 900,
          "table should grow with content width");
    Check(ReadingTableWidth(1400, 1800, 1.0, 1.0) == 1032,
          "wide table should be capped at 1032 DIP");
    Check(ReadingTableWidth(1400, 700, 1.0, 1.0) == 700,
          "table should be clipped to available width");
    Check(ReadingTableWidth(500, 2200, 1.0, 2.0) == 1680,
          "table minimum should scale with font zoom");
    Check(ReadingTableWidth(2200, 3000, 1.0, 2.0) == 2064,
          "table maximum should scale with font zoom");
    Check(CenteredReadingLeft(1800, 1032) == 384,
          "wide table should be centered");

    if (failures != 0) {
        std::cerr << "reading_layout_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "reading_layout_tests: passed\n";
    return EXIT_SUCCESS;
}
