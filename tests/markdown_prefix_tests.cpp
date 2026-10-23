#include "core/parser/MarkdownPrefix.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "markdown_prefix_tests: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    constexpr std::size_t kLimit = 10 * 1024;
    const std::string paragraph =
        std::string(9000, 'a') + "\n\n" + std::string(4000, 'b') + "\n";
    const std::size_t paragraphBoundary =
        litemark::FindSafeMarkdownPrefixBoundary(paragraph, kLimit);
    Check(paragraphBoundary == 9002,
          "paragraph should end at the last top-level blank line");

    const std::string fenced =
        std::string(9000, 'a') + "\n\n```md\n\n# not top level\n\n```\n" +
        std::string(4000, 'b') + "\n";
    const std::size_t fenceBoundary =
        litemark::FindSafeMarkdownPrefixBoundary(fenced, kLimit);
    Check(fenceBoundary > 9002,
          "closed fenced code should not cut at internal blank lines");

    const std::string quoted =
        std::string(9000, 'a') + "\n\n> # quoted\n\n" +
        std::string(4000, 'b') + "\n";
    const std::size_t quotedBoundary =
        litemark::FindSafeMarkdownPrefixBoundary(quoted, kLimit);
    Check(quotedBoundary > 9002,
          "block quote heading must not become a top-level boundary");

    Check(litemark::FindSafeMarkdownPrefixBoundary(
              "# short\n\nbody\n", kLimit) == 0,
          "short documents should not use a partial prefix");

    if (failures != 0) {
        return EXIT_FAILURE;
    }
    std::cout << "markdown_prefix_tests: passed\n";
    return EXIT_SUCCESS;
}
