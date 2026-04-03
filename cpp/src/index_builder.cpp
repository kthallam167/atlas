#include "atlas/index_builder.hpp"
#include "atlas/tokenizer.hpp"
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
