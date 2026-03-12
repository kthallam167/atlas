#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace atlas {

inline void vbyte_encode(uint64_t value, std::vector<uint8_t>& out) {
    while (value >= 0x80) {
        out.push_back(static_cast<uint8_t>(value & 0x7F));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value | 0x80));
}

inline uint64_t vbyte_decode(const uint8_t* data, size_t& pos) {
    uint64_t value = 0;
    int shift = 0;
    while (true) {
        uint8_t b = data[pos++];
        value |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (b & 0x80) break;
        shift += 7;
    }
    return value;
}

}  // namespace atlas
