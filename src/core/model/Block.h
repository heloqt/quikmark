#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace litemark {

using BlockId = std::uint64_t;

enum class BlockKind {
    Text,
    Heading,
    Code,
    Html,
    Table,
    Image,
    Flowchart,
    Rule
};

struct TextPosition {
    BlockId blockId = 0;
    std::uint64_t utf8ByteOffset = 0;
};

struct InlineSpan {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    bool code = false;
    bool image = false;
    std::string href;
};

struct TableCell {
    std::vector<InlineSpan> spans;
};

struct Block {
    BlockId id = 0;
    BlockKind kind = BlockKind::Text;
    int headingLevel = 0;
    int quoteDepth = 0;
    int listDepth = 0;
    int listIndex = 0;
    bool listOrdered = false;
    bool listMarker = false;
    bool task = false;
    bool taskChecked = false;
    bool continuesPreviousBlock = false;
    bool continuesNextBlock = false;
    std::vector<InlineSpan> spans;
    std::string raw;
    std::string language;
    std::string imagePath;
    std::string imageAlt;
    std::string flowSource;
    std::vector<std::vector<TableCell>> tableRows;
    std::vector<std::size_t> tableColumnWidthHints;
    std::size_t tableRowOffset = 0;
    bool tableFirstRowIsHeader = true;
};

struct MarkdownDocument {
    std::vector<Block> blocks;
};

}  // namespace litemark
