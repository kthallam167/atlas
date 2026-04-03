#include "atlas/index_builder.hpp"
#include "atlas/tokenizer.hpp"
#include "atlas/varbyte.hpp"
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace atlas {
namespace {

struct TermPostings {
    std::vector<uint32_t> docids;
    std::vector<uint32_t> freqs;
    std::vector<uint32_t> positions;
};

constexpr uint32_t kSkipThreshold = 16;

std::vector<uint8_t> encode_block(const TermPostings& tp) {
    const uint32_t df = static_cast<uint32_t>(tp.docids.size());
    std::vector<uint8_t> region;
    uint32_t prev_doc = 0;
    size_t pi = 0;

    for (uint32_t i = 0; i < df; ++i) {
        vbyte_encode(tp.docids[i] - prev_doc, region);
        prev_doc = tp.docids[i];
        const uint32_t tf = tp.freqs[i];
        vbyte_encode(tf, region);
        uint32_t prev_pos = 0;
        for (uint32_t j = 0; j < tf; ++j) {
            const uint32_t p = tp.positions[pi++];
            vbyte_encode(p - prev_pos, region);
            prev_pos = p;
        }
    }
    return region;
}

}  // namespace

BuildStats build_index(const std::string& tsv_path, const std::string& out_dir) {
    std::ifstream in(tsv_path);
    if (!in) throw std::runtime_error("cannot open: " + tsv_path);

    std::unordered_map<std::string, TermPostings> postings_map;
    BuildStats stats;
    std::string line;
    uint32_t doc_id = 0;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto tokens = tokenize(line);
        stats.num_docs++;
        stats.total_tokens += tokens.size();
        doc_id++;
    }
    stats.num_terms = postings_map.size();
    return stats;
}

}  // namespace atlas
