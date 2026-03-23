// Minimal self-contained test suite for the atlas core.
#include "atlas/tokenizer.hpp"
#include "atlas/varbyte.hpp"
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static void test_varbyte() {
    for (uint64_t v : {0ull, 1ull, 127ull, 128ull, 300ull, 16384ull, 1ull << 40}) {
        std::vector<uint8_t> buf;
        atlas::vbyte_encode(v, buf);
        CHECK(buf.size() == atlas::vbyte_size(v));
        size_t pos = 0;
        CHECK(atlas::vbyte_decode(buf.data(), pos) == v);
        CHECK(pos == buf.size());
    }
}

static void test_tokenizer() {
    const auto toks = atlas::tokenize("Hello, World! 123-testing.");
    CHECK(toks == (std::vector<std::string>{"hello", "world", "123", "testing"}));
}

int main() {
    test_varbyte();
    test_tokenizer();
    return g_failures ? 1 : 0;
}
