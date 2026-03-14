// Minimal self-contained test suite for the atlas core.
#include "atlas/varbyte.hpp"
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static void test_varbyte() {
    for (uint64_t v : {0ull, 1ull, 127ull, 128ull, 300ull, 16384ull}) {
        std::vector<uint8_t> buf;
        atlas::vbyte_encode(v, buf);
        size_t pos = 0;
        CHECK(atlas::vbyte_decode(buf.data(), pos) == v);
        CHECK(pos == buf.size());
    }
}

int main() {
    test_varbyte();
    return g_failures ? 1 : 0;
}
