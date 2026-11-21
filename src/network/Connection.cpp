#include "network/Connection.hpp"

#include <algorithm>

namespace perlite {

    std::vector<std::vector<uint8_t>> SplitFragments(const uint8_t* data, size_t size, size_t partSize) {
        std::vector<std::vector<uint8_t>> out;
        if (size == 0)
            return out;
        if (partSize == 0)
            partSize = kFragmentSize;
        const size_t count = (size - 1) / partSize + 1;
        if (count > 256)
            return out;
        for (size_t i = 0; i < count; i++) {
            const size_t off = i * partSize;
            const size_t len = std::min(partSize, size - off);
            std::vector<uint8_t> part;
            part.reserve(len + 1);
            part.push_back(static_cast<uint8_t>(count - 1 - i));
            part.insert(part.end(), data + off, data + off + len);
            out.push_back(std::move(part));
        }
        return out;
    }
}
