#pragma once
// Builds an on-disk index from a TSV corpus. Each input line is one document:
//   title <TAB> url <TAB> text
// The document id is the line number (0-based). Postings accumulate in memory
// and are flushed to the compressed on-disk format in one pass at the end.
#include <string>
#include <cstdint>

namespace atlas {

struct BuildStats {
    uint64_t num_docs = 0;
    uint64_t num_terms = 0;
    uint64_t total_tokens = 0;
    uint64_t postings_bytes = 0;   // compressed size on disk
    uint64_t naive_bytes = 0;      // fixed 4-byte ints, no gaps/compression
    double   build_seconds = 0.0;
};

// Build an index at `out_dir` from `tsv_path`. Creates the directory if needed.
BuildStats build_index(const std::string& tsv_path, const std::string& out_dir);

}  // namespace atlas
