#pragma once

#include <algorithm>
#include <optional>

namespace litemark {

// A requested scroll offset where zero is a real request, not an empty state.
class PendingScrollPosition {
public:
    void Request(int position) {
        position_ = std::max(0, position);
    }

    void Clear() {
        position_.reset();
    }

    bool HasPending() const {
        return position_.has_value();
    }

    std::optional<int> Take() {
        const std::optional<int> value = position_;
        position_.reset();
        return value;
    }

private:
    std::optional<int> position_;
};

}  // namespace litemark
