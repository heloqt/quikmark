#pragma once

#include "core/model/Block.h"

#include <atomic>
#include <memory>
#include <string>

namespace litemark {

class MarkdownParser {
public:
    static std::shared_ptr<const MarkdownDocument> Parse(
        const std::string& utf8,
        std::string* error = nullptr,
        const std::atomic_bool* cancelled = nullptr);
};

}  // namespace litemark
