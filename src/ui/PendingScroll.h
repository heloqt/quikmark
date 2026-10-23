#pragma once

#include <algorithm>
#include <optional>

namespace litemark {

// A requested scroll offset where zero is a real request, not an empty state.
class PendingScrollPosition {
public:
    void Request(int position) {
        position_ = std::max(0, position);
        applying_.reset();
    }

    void Clear() {
        position_.reset();
        applying_.reset();
    }

    bool HasPending() const {
        return position_.has_value();
    }

    std::optional<int> Peek() const {
        return position_ ? position_ : applying_;
    }

    std::optional<int> Take() {
        const std::optional<int> value = position_;
        position_.reset();
        applying_ = value;
        return value;
    }

    void MarkApplied() {
        applying_.reset();
    }

private:
    std::optional<int> position_;
    std::optional<int> applying_;
};

}  // namespace litemark
