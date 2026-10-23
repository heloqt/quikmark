#include "ui/PendingScroll.h"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "pending_scroll_tests: " << message << '\n';
        ++failures;
    }
}

void TestStartsEmpty() {
    litemark::PendingScrollPosition pending;
    Check(!pending.HasPending(), "new state should have no pending position");
    Check(!pending.Take().has_value(), "taking an empty state should return empty");
}

void TestZeroIsAppliedAsARealRequest() {
    litemark::PendingScrollPosition pending;
    pending.Request(0);
    Check(pending.HasPending(), "zero must be retained as a pending request");
    const auto value = pending.Take();
    Check(value.has_value() && *value == 0,
          "zero must be returned instead of being treated as empty");
}

void TestPositivePositionIsPreserved() {
    litemark::PendingScrollPosition pending;
    pending.Request(1830);
    Check(pending.HasPending(), "positive request should be pending");
    const auto value = pending.Take();
    Check(value.has_value() && *value == 1830,
          "positive request should be preserved");
}

void TestNegativePositionClampsToTop() {
    litemark::PendingScrollPosition pending;
    pending.Request(-120);
    const auto value = pending.Take();
    Check(value.has_value() && *value == 0,
          "negative request should clamp to zero");
}

void TestLatestRequestWins() {
    litemark::PendingScrollPosition pending;
    pending.Request(900);
    pending.Request(0);
    const auto value = pending.Take();
    Check(value.has_value() && *value == 0,
          "a later zero request should replace an earlier offset");
}

void TestTakeClearsTheRequest() {
    litemark::PendingScrollPosition pending;
    pending.Request(420);
    Check(pending.Peek().has_value() && *pending.Peek() == 420,
          "peek should expose the request without consuming it");
    Check(pending.HasPending(), "peek should not clear the request");
    Check(pending.Take().has_value(), "first take should return the request");
    Check(pending.Peek().has_value() && *pending.Peek() == 420,
          "taken request should remain requested until marked applied");
    pending.MarkApplied();
    Check(!pending.Peek().has_value(),
          "mark applied should clear the in-flight request");
    Check(!pending.HasPending(), "take should clear the request");
    Check(!pending.Take().has_value(), "second take should return empty");
}

void TestClearDiscardsTheRequest() {
    litemark::PendingScrollPosition pending;
    pending.Request(0);
    pending.Clear();
    Check(!pending.HasPending(), "clear should discard a zero request");
}

}  // namespace

int main() {
    TestStartsEmpty();
    TestZeroIsAppliedAsARealRequest();
    TestPositivePositionIsPreserved();
    TestNegativePositionClampsToTop();
    TestLatestRequestWins();
    TestTakeClearsTheRequest();
    TestClearDiscardsTheRequest();

    if (failures != 0) {
        std::cerr << "pending_scroll_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "pending_scroll_tests: 7 cases passed\n";
    return EXIT_SUCCESS;
}
