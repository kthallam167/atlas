#pragma once
// Evaluates a parsed query against an index and ranks matches with BM25.
#include "atlas/index.hpp"
#include "atlas/query_parser.hpp"

#include <string>
#include <vector>

namespace atlas {

struct SearchResult {
    uint32_t docid;
    double score;
    std::string title;
    std::string url;
};

struct Bm25Params {
    double k1 = 1.2;
    double b = 0.75;
};

class Searcher {
public:
    explicit Searcher(const Index& index, Bm25Params params = {})
        : index_(index), params_(params) {}

    // Parse `query`, find matching documents, return the top-`k` by BM25.
    std::vector<SearchResult> search(const std::string& query, size_t k) const;

    // Boolean evaluation only: sorted list of matching doc ids.
    std::vector<uint32_t> evaluate(const QueryNode& node) const;

private:
    std::vector<uint32_t> term_docs(const std::string& term) const;
    std::vector<uint32_t> phrase_docs(const std::vector<std::string>& terms) const;

    const Index& index_;
    Bm25Params params_;
};

}  // namespace atlas
