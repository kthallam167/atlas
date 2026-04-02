#include "atlas/index_builder.hpp"
#include "atlas/tokenizer.hpp"
#include <fstream>
#include <stdexcept>

namespace atlas {

BuildStats build_index(const std::string& tsv_path, const std::string& out_dir) {
    std::ifstream in(tsv_path);
    if (!in) throw std::runtime_error("cannot open: " + tsv_path);

    BuildStats stats;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        stats.num_docs++;
    }
    return stats;
}

}  // namespace atlas
