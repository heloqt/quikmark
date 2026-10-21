#pragma once

#include <cstdint>

namespace litemark {

using BlockId = std::uint64_t;

enum class BlockKind {
    Text,
    Table,
    Image,
    Flowchart,
    Rule
};

struct TextPosition {
    BlockId blockId = 0;
    std::uint64_t utf8ByteOffset = 0;
};

class Block {
public:
    virtual ~Block() = default;
    virtual BlockId id() const = 0;
    virtual std::uint64_t semanticId() const = 0;
    virtual BlockKind kind() const = 0;
};

}  // namespace litemark
