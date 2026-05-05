#include "atlas/searcher.hpp"
#include <algorithm>
#include <cmath>

namespace atlas {

std::vector<SearchResult> Searcher::search(const std::string& query, size_t k) const {
    std::vector<SearchResult> results;
    auto node = parse_query(query);
    if (!node) return results;

    const auto docs = evaluate(*node);
    for (size_t i = 0; i < std::min(docs.size(), k); ++i) {
        const uint32_t docid = docs[i];
        results.push_back({docid, 1.0, index_.doc_title(docid), index_.doc_url(docid)});
    }
    return results;
}

std::vector<uint32_t> Searcher::evaluate(const QueryNode& node) const {
    if (node.type == NodeType::Term) return term_docs(node.term);
    return {};
}

std::vector<uint32_t> Searcher::term_docs(const std::string& term) const {
    std::vector<uint32_t> docs;
    const auto* te = index_.find_term(term);
    if (!te) return docs;
    auto iter = index_.postings(*te);
    while (!iter.at_end()) {
        docs.push_back(iter.docid());
        iter.next();
    }
    return docs;
}

std::vector<uint32_t> Searcher::phrase_docs(const std::vector<std::string>& terms) const {
    return {};
}

}  // namespace atlas
