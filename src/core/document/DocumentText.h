#pragma once

#include "core/model/Block.h"

#include <cstddef>
#include <string>
#include <vector>

namespace litemark {

struct TextSelectionPoint {
    std::size_t blockIndex = 0;
    std::size_t utf8Offset = 0;
};

struct TextSelectionRange {
    TextSelectionPoint start;
    TextSelectionPoint end;
};

struct TableCellTextRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

std::size_t ClampUtf8Offset(const std::string& text, std::size_t offset);

std::string TableCellPlainText(const TableCell& cell);

std::vector<TableCellTextRange> TableCellTextRanges(const Block& block);

std::string BlockPlainText(const Block& block);

std::string PlainTextForSelection(const MarkdownDocument& document,
                                  TextSelectionRange range);

TextSelectionRange NormalizeSelection(const MarkdownDocument& document,
                                      TextSelectionRange range);

bool IsEmptySelection(const MarkdownDocument& document,
                      TextSelectionRange range);

}  // namespace litemark
