#pragma once

#include "core/model/Block.h"

#include <cstddef>
#include <string>

namespace litemark {

struct TextSelectionPoint {
    std::size_t blockIndex = 0;
    std::size_t utf8Offset = 0;
};

struct TextSelectionRange {
    TextSelectionPoint start;
    TextSelectionPoint end;
};

std::size_t ClampUtf8Offset(const std::string& text, std::size_t offset);

std::string BlockPlainText(const Block& block);

std::string PlainTextForSelection(const MarkdownDocument& document,
                                  TextSelectionRange range);

TextSelectionRange NormalizeSelection(const MarkdownDocument& document,
                                      TextSelectionRange range);

bool IsEmptySelection(const MarkdownDocument& document,
                      TextSelectionRange range);

}  // namespace litemark
