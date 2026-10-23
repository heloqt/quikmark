#include "core/layout/ListLayout.h"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "list_layout_tests: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    const auto normal =
        litemark::ComputeListLayout(1, false, 1, 1.0, 1.0);
    Check(normal.markerOffset == 4,
          "first-level bullet should start at L + 4 DIP");
    Check(normal.markerGutter == 28 && normal.textOffset == 32,
          "first-level list text should follow a 28 DIP marker gutter");

    const auto nested =
        litemark::ComputeListLayout(2, false, 1, 1.0, 1.0);
    Check(nested.markerOffset == 26 && nested.textOffset == 54,
          "nested list should add one 22 DIP level");

    const auto ordered =
        litemark::ComputeListLayout(1, true, 100, 1.0, 1.0);
    Check(ordered.markerGutter == 34 && ordered.textOffset == 38,
          "ordered marker gutter should grow for three-digit numbers");

    const auto scaled =
        litemark::ComputeListLayout(1, true, 100, 1.0, 2.0);
    Check(scaled.markerOffset == 8 && scaled.markerGutter == 68 &&
              scaled.textOffset == 76,
          "list metrics should scale with font zoom");

    const auto outsideList =
        litemark::ComputeListLayout(0, false, 1, 1.0, 1.0);
    Check(outsideList.markerOffset == 0 &&
              outsideList.markerGutter == 0 &&
              outsideList.textOffset == 0,
          "non-list blocks must not receive list indentation");

    if (failures != 0) {
        return EXIT_FAILURE;
    }
    std::cout << "list_layout_tests: passed\n";
    return EXIT_SUCCESS;
}
