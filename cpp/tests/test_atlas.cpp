// Minimal self-contained test suite for the atlas core.
#include "atlas/index.hpp"
#include "atlas/index_builder.hpp"
#include "atlas/searcher.hpp"
#include "atlas/tokenizer.hpp"
#include "atlas/varbyte.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static std::vector<uint32_t> ids(const std::vector<atlas::SearchResult>& r) {
    std::vector<uint32_t> v;
    for (const auto& x : r) v.push_back(x.docid);
    return v;
}
static bool contains(const std::vector<uint32_t>& v, uint32_t x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

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
    const auto t = atlas::tokenize("Hello, WORLD! 42 e-mail");
    CHECK((t == std::vector<std::string>{"hello", "world", "42", "e", "mail"}));
}

// Write a small corpus and return its index directory.
static std::string build_fixture(const fs::path& root) {
    const fs::path tsv = root / "corpus.tsv";
    std::ofstream f(tsv);
    // title \t url \t text
    f << "Cats\thttp://x/cat\tthe quick brown fox jumps\n";              // 0
    f << "Dogs\thttp://x/dog\tthe lazy dog sleeps all day\n";            // 1
    f << "Foxes\thttp://x/fox\ta quick brown fox and a quick hare\n";    // 2
    f << "Weather\thttp://x/wx\tthe brown storm rolls in today\n";       // 3
    f.close();
    const std::string dir = (root / "index").string();
    atlas::build_index(tsv.string(), dir);
    return dir;
}

static void test_search() {
    const fs::path root = fs::temp_directory_path() / "atlas_test_search";
    fs::remove_all(root); fs::create_directories(root);
    const std::string dir = build_fixture(root);

    atlas::Index index(dir);
    CHECK(index.num_docs() == 4);
    atlas::Searcher s(index);

    // Single term.
    CHECK(ids(s.search("fox", 10)).size() == 2);
    // AND: brown fox -> docs 0 and 2.
    { auto r = ids(s.search("brown AND fox", 10)); CHECK(r.size() == 2);
      CHECK(contains(r, 0)); CHECK(contains(r, 2)); }
    // Implicit AND behaves the same.
    CHECK(ids(s.search("brown fox", 10)).size() == 2);
    // OR: dog OR storm -> docs 1 and 3.
    { auto r = ids(s.search("dog OR storm", 10)); CHECK(r.size() == 2);
      CHECK(contains(r, 1)); CHECK(contains(r, 3)); }
    // NOT: brown but not fox -> doc 3 only (docs 0,2 have fox).
    { auto r = ids(s.search("brown AND NOT fox", 10)); CHECK(r.size() == 1);
      CHECK(contains(r, 3)); }
    // Phrase: "quick brown" -> docs 0 and 2.
    { auto r = ids(s.search("\"quick brown\"", 10)); CHECK(r.size() == 2);
      CHECK(contains(r, 0)); CHECK(contains(r, 2)); }
    // Phrase that does not occur consecutively.
    CHECK(ids(s.search("\"brown quick\"", 10)).empty());
    // Missing term.
    CHECK(ids(s.search("nonexistentterm", 10)).empty());

    fs::remove_all(root);
}

// Force a long postings list so skip pointers are exercised by advance().
static void test_skips() {
    const fs::path root = fs::temp_directory_path() / "atlas_test_skips";
    fs::remove_all(root); fs::create_directories(root);
    const fs::path tsv = root / "corpus.tsv";
    std::ofstream f(tsv);
    const int N = 500;
    for (int i = 0; i < N; ++i) {
        // "common" in every doc; "rare" only in multiples of 50.
        f << "doc" << i << "\thttp://x/" << i << "\tcommon word body";
        if (i % 50 == 0) f << " rare";
        f << "\n";
    }
    f.close();
    const std::string dir = (root / "index").string();
    atlas::build_index(tsv.string(), dir);

    atlas::Index index(dir);
    atlas::Searcher s(index);
    // "common" list is long (skips built); AND with "rare" must intersect via advance().
    auto r = ids(s.search("common AND rare", 100));
    CHECK(r.size() == 10);
    for (uint32_t d : r) CHECK(d % 50 == 0);

    fs::remove_all(root);
}

static void test_invalid_queries() {
    const fs::path root = fs::temp_directory_path() / "atlas_test_invalid";
    fs::remove_all(root); fs::create_directories(root);
    const std::string dir = build_fixture(root);
    atlas::Index index(dir);
    atlas::Searcher s(index);

    bool caught = false;
    try {
        s.search("(quick AND brown", 10);
    } catch (const std::runtime_error&) {
        caught = true;
    }
    CHECK(caught);

    fs::remove_all(root);
}

int main() {
    test_varbyte();
    test_tokenizer();
    test_search();
    test_skips();
    test_invalid_queries();
    if (g_failures == 0) std::printf("all tests passed\n");
    else std::printf("%d checks failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
