// Command-line driver for building, querying, and benchmarking an index.
#include "atlas/index.hpp"
#include "atlas/index_builder.hpp"
#include "atlas/searcher.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using clk = std::chrono::steady_clock;
static double ms_since(clk::time_point t) {
    return std::chrono::duration<double, std::milli>(clk::now() - t).count();
}

static int cmd_build(const std::string& corpus, const std::string& dir) {
    const atlas::BuildStats st = atlas::build_index(corpus, dir);
    const double ratio = st.naive_bytes ? 100.0 * (1.0 - double(st.postings_bytes) / st.naive_bytes) : 0.0;
    std::printf("built index at %s\n", dir.c_str());
    std::printf("  documents      : %llu\n", (unsigned long long)st.num_docs);
    std::printf("  unique terms   : %llu\n", (unsigned long long)st.num_terms);
    std::printf("  total tokens   : %llu\n", (unsigned long long)st.total_tokens);
    std::printf("  postings size  : %.2f MB (compressed)\n", st.postings_bytes / 1e6);
    std::printf("  naive size     : %.2f MB (fixed 4-byte ints)\n", st.naive_bytes / 1e6);
    std::printf("  index shrink   : %.1f%% smaller than naive\n", ratio);
    std::printf("  build time     : %.2f s\n", st.build_seconds);
    return 0;
}

static int cmd_search(const std::string& dir, const std::string& query, size_t k) {
    const auto t_open = clk::now();
    atlas::Index index(dir);
    const double open_ms = ms_since(t_open);

    atlas::Searcher searcher(index);
    const auto t_q = clk::now();
    const auto results = searcher.search(query, k);
    const double q_ms = ms_since(t_q);

    std::printf("cold-start (mmap open): %.2f ms\n", open_ms);
    std::printf("query \"%s\" -> %zu results in %.3f ms\n\n", query.c_str(), results.size(), q_ms);
    int rank = 1;
    for (const auto& r : results) {
        std::printf("%2d. [%.4f] %s\n", rank++, r.score, r.title.c_str());
        if (!r.url.empty()) std::printf("        %s\n", r.url.c_str());
    }
    return 0;
}

static int cmd_bench(const std::string& dir, const std::string& queries_path, size_t k) {
    const auto t_open = clk::now();
    atlas::Index index(dir);
    const double open_ms = ms_since(t_open);

    std::ifstream qf(queries_path);
    if (!qf) { std::fprintf(stderr, "cannot open queries: %s\n", queries_path.c_str()); return 1; }
    std::vector<std::string> queries;
    std::string line;
    while (std::getline(qf, line)) if (!line.empty()) queries.push_back(line);
    if (queries.empty()) { std::fprintf(stderr, "no queries\n"); return 1; }

    atlas::Searcher searcher(index);
    std::vector<double> lat;
    lat.reserve(queries.size());
    const auto t_all = clk::now();
    size_t hits = 0;
    for (const auto& q : queries) {
        const auto t = clk::now();
        hits += searcher.search(q, k).size();
        lat.push_back(ms_since(t));
    }
    const double total_ms = ms_since(t_all);
    std::sort(lat.begin(), lat.end());
    const double p50 = lat[lat.size() / 2];
    const double p95 = lat[std::min(lat.size() - 1, size_t(lat.size() * 0.95))];
    double sum = 0; for (double x : lat) sum += x;

    std::printf("cold-start (mmap open): %.2f ms\n", open_ms);
    std::printf("queries        : %zu (%zu total hits)\n", queries.size(), hits);
    std::printf("throughput     : %.0f queries/sec\n", 1000.0 * queries.size() / total_ms);
    std::printf("latency mean   : %.3f ms\n", sum / lat.size());
    std::printf("latency p50    : %.3f ms\n", p50);
    std::printf("latency p95    : %.3f ms\n", p95);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage:\n"
            "  atlas build  <corpus.tsv> <index_dir>\n"
            "  atlas search <index_dir> \"<query>\" [k=10]\n"
            "  atlas bench  <index_dir> <queries.txt> [k=10]\n");
        return 1;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "build" && argc == 4) return cmd_build(argv[2], argv[3]);
        if (cmd == "search" && argc >= 4)
            return cmd_search(argv[2], argv[3], argc >= 5 ? std::stoul(argv[4]) : 10);
        if (cmd == "bench" && argc >= 4)
            return cmd_bench(argv[2], argv[3], argc >= 5 ? std::stoul(argv[4]) : 10);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "invalid arguments; run with no args for usage\n");
    return 1;
}
