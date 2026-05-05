#include "atlas/searcher.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace atlas {
namespace {

// A doc-id cursor used by AND intersection. Term children stay lazy and use
// skip pointers; other sub-expressions are materialized into a sorted vector.
struct Cursor {
    virtual ~Cursor() = default;
    virtual uint32_t doc() const = 0;
    virtual void advance(uint32_t target) = 0;  // move to first doc >= target
    virtual uint32_t size_hint() const = 0;
};

struct TermCursor : Cursor {
    PostingsIterator it;
    explicit TermCursor(PostingsIterator i) : it(std::move(i)) {}
    uint32_t doc() const override { return it.docid(); }
    void advance(uint32_t target) override { it.advance(target); }
    uint32_t size_hint() const override { return it.doc_freq(); }
};

struct VecCursor : Cursor {
    std::vector<uint32_t> docs;
    size_t pos = 0;
    explicit VecCursor(std::vector<uint32_t> d) : docs(std::move(d)) {}
    uint32_t doc() const override { return pos < docs.size() ? docs[pos] : kNoDoc; }
    void advance(uint32_t target) override {
        pos = std::lower_bound(docs.begin() + pos, docs.end(), target) - docs.begin();
    }
    uint32_t size_hint() const override { return static_cast<uint32_t>(docs.size()); }
};

// k-way intersection over doc-id cursors.
std::vector<uint32_t> intersect(std::vector<std::unique_ptr<Cursor>>& cs) {
    std::vector<uint32_t> out;
    if (cs.empty()) return out;
    // Drive from the shortest list to minimize work.
    std::sort(cs.begin(), cs.end(), [](const auto& a, const auto& b) {
        return a->size_hint() < b->size_hint();
    });
    while (cs[0]->doc() != kNoDoc) {
        const uint32_t candidate = cs[0]->doc();
        bool all = true;
        for (size_t i = 1; i < cs.size(); ++i) {
            cs[i]->advance(candidate);
            if (cs[i]->doc() != candidate) { all = false; break; }
        }
        if (all) out.push_back(candidate);
        cs[0]->advance(candidate + 1);
    }
    return out;
}

std::vector<uint32_t> union_lists(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    std::vector<uint32_t> out;
    out.reserve(a.size() + b.size());
    std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

// Collect the terms that contribute to relevance (everything not under NOT).
void collect_scoring_terms(const QueryNode& n, bool negated, std::vector<std::string>& out) {
    switch (n.type) {
        case NodeType::Term:
            if (!negated) out.push_back(n.term);
            break;
        case NodeType::Phrase:
            if (!negated) for (const auto& t : n.phrase) out.push_back(t);
            break;
        case NodeType::Not:
            collect_scoring_terms(*n.child, !negated, out);
            break;
        case NodeType::And:
        case NodeType::Or:
            for (const auto& c : n.children) collect_scoring_terms(*c, negated, out);
            break;
    }
}

}  // namespace

std::vector<uint32_t> Searcher::term_docs(const std::string& term) const {
    std::vector<uint32_t> out;
    const TermEntry* te = index_.find_term(term);
    if (!te) return out;
    out.reserve(te->doc_freq);
    PostingsIterator it = index_.postings(*te);
    for (; !it.at_end(); it.next()) out.push_back(it.docid());
    return out;
}

std::vector<uint32_t> Searcher::phrase_docs(const std::vector<std::string>& terms) const {
    if (terms.empty()) return {};
    if (terms.size() == 1) return term_docs(terms[0]);

    std::vector<PostingsIterator> its;
    its.reserve(terms.size());
    for (const auto& t : terms) {
        const TermEntry* te = index_.find_term(t);
        if (!te) return {};  // a missing term can't form the phrase
        its.push_back(index_.postings(*te));
    }

    std::vector<uint32_t> out;
    while (true) {
        // Align all iterators on a common doc.
        uint32_t candidate = 0;
        for (auto& it : its) if (it.at_end()) return out; else candidate = std::max(candidate, it.docid());
        bool aligned = true;
        for (auto& it : its) {
            it.advance(candidate);
            if (it.at_end()) return out;
            if (it.docid() != candidate) { aligned = false; break; }
        }
        if (aligned) {
            // Positional check: some base p with term[i] at p+i for all i.
            const std::vector<uint32_t> base = its[0].positions();
            std::vector<std::vector<uint32_t>> rest(its.size());
            for (size_t i = 1; i < its.size(); ++i) rest[i] = its[i].positions();
            for (uint32_t p : base) {
                bool ok = true;
                for (size_t i = 1; i < its.size() && ok; ++i) {
                    ok = std::binary_search(rest[i].begin(), rest[i].end(), p + static_cast<uint32_t>(i));
                }
                if (ok) { out.push_back(candidate); break; }
            }
            for (auto& it : its) it.advance(candidate + 1);
        }
    }
}

std::vector<uint32_t> Searcher::evaluate(const QueryNode& node) const {
    switch (node.type) {
        case NodeType::Term:
            return term_docs(node.term);
        case NodeType::Phrase:
            return phrase_docs(node.phrase);
        case NodeType::Or: {
            std::vector<uint32_t> acc;
            for (const auto& c : node.children) acc = union_lists(acc, evaluate(*c));
            return acc;
        }
        case NodeType::Not: {
            const std::vector<uint32_t> excluded = evaluate(*node.child);
            std::vector<uint32_t> out;
            const uint32_t n = static_cast<uint32_t>(index_.num_docs());
            size_t ei = 0;
            for (uint32_t d = 0; d < n; ++d) {
                if (ei < excluded.size() && excluded[ei] == d) { ++ei; continue; }
                out.push_back(d);
            }
            return out;
        }
        case NodeType::And: {
            std::vector<std::unique_ptr<Cursor>> cs;
            for (const auto& c : node.children) {
                if (c->type == NodeType::Term) {
                    const TermEntry* te = index_.find_term(c->term);
                    if (!te) return {};  // empty term short-circuits the AND
                    cs.push_back(std::make_unique<TermCursor>(index_.postings(*te)));
                } else {
                    std::vector<uint32_t> v = evaluate(*c);
                    if (v.empty()) return {};
                    cs.push_back(std::make_unique<VecCursor>(std::move(v)));
                }
            }
            return intersect(cs);
        }
    }
    return {};
}

std::vector<SearchResult> Searcher::search(const std::string& query, size_t k) const {
    auto ast = parse_query(query);
    if (!ast) return {};

    const std::vector<uint32_t> candidates = evaluate(*ast);
    if (candidates.empty()) return {};
    const std::unordered_set<uint32_t> candidate_set(candidates.begin(), candidates.end());

    std::vector<std::string> terms;
    collect_scoring_terms(*ast, false, terms);
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    const double N = static_cast<double>(index_.num_docs());
    const double avgdl = index_.avg_doc_len();
    std::unordered_map<uint32_t, double> scores;
    scores.reserve(candidates.size() * 2);
    for (uint32_t d : candidates) scores[d] = 0.0;

    for (const auto& t : terms) {
        const TermEntry* te = index_.find_term(t);
        if (!te) continue;
        const double idf = std::log((N - te->doc_freq + 0.5) / (te->doc_freq + 0.5) + 1.0);
        PostingsIterator it = index_.postings(*te);
        for (; !it.at_end(); it.next()) {
            auto found = scores.find(it.docid());
            if (found == scores.end()) continue;
            const double tf = it.freq();
            const double dl = index_.doc_length(it.docid());
            const double denom = tf + params_.k1 * (1.0 - params_.b + params_.b * dl / avgdl);
            found->second += idf * (tf * (params_.k1 + 1.0)) / denom;
        }
    }

    std::vector<SearchResult> results;
    results.reserve(candidates.size());
    for (uint32_t d : candidates) results.push_back({d, scores[d], "", ""});

    const size_t topk = std::min(k, results.size());
    std::partial_sort(results.begin(), results.begin() + topk, results.end(),
                      [](const SearchResult& a, const SearchResult& b) {
                          if (a.score != b.score) return a.score > b.score;
                          return a.docid < b.docid;
                      });
    results.resize(topk);
    for (auto& r : results) {
        r.title = index_.doc_title(r.docid);
        r.url = index_.doc_url(r.docid);
    }
    return results;
}

}  // namespace atlas
