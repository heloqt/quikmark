#pragma once

#include "core/model/Block.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

namespace litemark {

class MarkdownParser {
public:
    static std::shared_ptr<const MarkdownDocument> Parse(
        std::string_view utf8,
        std::string* error = nullptr,
        const std::atomic_bool* cancelled = nullptr,
        double* md4cMicroseconds = nullptr,
        double* normalizeMicroseconds = nullptr);
};

}  // namespace litemark
