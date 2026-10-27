#include "core/document/DocumentText.h"

#include <algorithm>

namespace {

void AppendSpans(std::string* output,
                 const std::vector<litemark::InlineSpan>& spans) {
    for (const auto& span : spans) {
        output->append(span.text);
    }
}

bool Before(const litemark::TextSelectionPoint& left,
            const litemark::TextSelectionPoint& right) {
    return left.blockIndex < right.blockIndex ||
           (left.blockIndex == right.blockIndex &&
            left.utf8Offset < right.utf8Offset);
}

}  // namespace

namespace litemark {

std::size_t ClampUtf8Offset(const std::string& text, std::size_t offset) {
    offset = std::min(offset, text.size());
    while (offset > 0 && offset < text.size() &&
           (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U) {
        --offset;
    }
    return offset;
}

std::string TableCellPlainText(const TableCell& cell) {
    std::string text;
    AppendSpans(&text, cell.spans);
    return text;
}

std::vector<TableCellTextRange> TableCellTextRanges(const Block& block) {
    std::size_t columnCount = 0;
    for (const auto& row : block.tableRows) {
        columnCount = std::max(columnCount, row.size());
    }
    if (block.tableRows.empty() || columnCount == 0) {
        return {};
    }

    std::vector<TableCellTextRange> ranges(
        block.tableRows.size() * columnCount);
    std::size_t offset = 0;
    for (std::size_t rowIndex = 0;
         rowIndex < block.tableRows.size(); ++rowIndex) {
        const auto& row = block.tableRows[rowIndex];
        for (std::size_t column = 0; column < columnCount; ++column) {
            TableCellTextRange& range =
                ranges[rowIndex * columnCount + column];
            range.begin = offset;
            range.end = offset;
            if (column >= row.size()) {
                continue;
            }
            range.end += TableCellPlainText(row[column]).size();
            offset = range.end;
            if (column + 1 < row.size()) {
                ++offset;
            }
        }
        if (rowIndex + 1 < block.tableRows.size()) {
            ++offset;
        }
    }
    return ranges;
}

std::string BlockPlainText(const Block& block) {
    switch (block.kind) {
        case BlockKind::Text:
        case BlockKind::Heading: {
            std::string text;
            AppendSpans(&text, block.spans);
            return text;
        }
        case BlockKind::Code:
        case BlockKind::Html:
            return block.raw;
        case BlockKind::Table: {
            std::string text;
            for (std::size_t rowIndex = 0;
                 rowIndex < block.tableRows.size(); ++rowIndex) {
                if (rowIndex > 0) {
                    text.push_back('\n');
                }
                const auto& row = block.tableRows[rowIndex];
                for (std::size_t cellIndex = 0;
                     cellIndex < row.size(); ++cellIndex) {
                    if (cellIndex > 0) {
                        text.push_back('\t');
                    }
                    text += TableCellPlainText(row[cellIndex]);
                }
            }
            return text;
        }
        case BlockKind::Image:
            return block.imageAlt;
        case BlockKind::Flowchart:
            return block.flowSource;
        case BlockKind::Rule:
            return {};
    }
    return {};
}

TextSelectionRange NormalizeSelection(const MarkdownDocument& document,
                                      TextSelectionRange range) {
    if (document.blocks.empty()) {
        return {};
    }

    range.start.blockIndex =
        std::min(range.start.blockIndex, document.blocks.size() - 1);
    range.end.blockIndex =
        std::min(range.end.blockIndex, document.blocks.size() - 1);
    range.start.utf8Offset = ClampUtf8Offset(
        BlockPlainText(document.blocks[range.start.blockIndex]),
        range.start.utf8Offset);
    range.end.utf8Offset = ClampUtf8Offset(
        BlockPlainText(document.blocks[range.end.blockIndex]),
        range.end.utf8Offset);
    if (Before(range.end, range.start)) {
        std::swap(range.start, range.end);
    }
    return range;
}

bool IsEmptySelection(const MarkdownDocument& document,
                      TextSelectionRange range) {
    if (document.blocks.empty()) {
        return true;
    }
    range = NormalizeSelection(document, range);
    return range.start.blockIndex == range.end.blockIndex &&
           range.start.utf8Offset == range.end.utf8Offset;
}

std::string PlainTextForSelection(const MarkdownDocument& document,
                                  TextSelectionRange range) {
    if (document.blocks.empty()) {
        return {};
    }
    range = NormalizeSelection(document, range);
    if (IsEmptySelection(document, range)) {
        return {};
    }

    std::string output;
    for (std::size_t index = range.start.blockIndex;
         index <= range.end.blockIndex; ++index) {
        std::string blockText = BlockPlainText(document.blocks[index]);
        const std::size_t begin =
            index == range.start.blockIndex ? range.start.utf8Offset : 0;
        const std::size_t end =
            index == range.end.blockIndex ? range.end.utf8Offset
                                           : blockText.size();
        if (begin >= end) {
            continue;
        }
        const bool joinsParagraph =
            index > range.start.blockIndex &&
            document.blocks[index].continuesPreviousBlock &&
            document.blocks[index].kind == BlockKind::Text &&
            document.blocks[index - 1].kind == BlockKind::Text;
        if (!output.empty() && !joinsParagraph) {
            output.push_back('\n');
        }
        output.append(blockText, begin, end - begin);
    }
    return output;
}

}  // namespace litemark
