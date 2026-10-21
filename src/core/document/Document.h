#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace litemark {

class Block;

class Document {
public:
    using Generation = std::uint64_t;

    Generation generation() const { return generation_; }

private:
    Generation generation_ = 0;
    std::string source_;
    std::vector<Block*> blocks_;
};

}  // namespace litemark
