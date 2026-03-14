#pragma once
// Variable-byte (VByte) integer coding. 7 payload bits per byte; the high bit
// marks the final byte of a value. Small integers -> 1 byte, which is why
// gap-encoded postings compress well.
#include <cstdint>
#include <cstddef>
#include <vector>

namespace atlas {

// Append `value` to `out` as a varbyte-coded integer.
inline void vbyte_encode(uint64_t value, std::vector<uint8_t>& out) {
    while (value >= 0x80) {
        out.push_back(static_cast<uint8_t>(value & 0x7F));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value | 0x80));
}

// Decode one varbyte value from `data` starting at `pos`; advances `pos`.
inline uint64_t vbyte_decode(const uint8_t* data, size_t& pos) {
    uint64_t value = 0;
    int shift = 0;
    while (true) {
        uint8_t b = data[pos++];
        value |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (b & 0x80) break;   // high bit set == last byte
        shift += 7;
    }
    return value;
}

// Number of bytes a value would occupy when varbyte-coded.
inline size_t vbyte_size(uint64_t value) {
    size_t n = 1;
    while (value >= 0x80) { value >>= 7; ++n; }
    return n;
}

}  // namespace atlas
