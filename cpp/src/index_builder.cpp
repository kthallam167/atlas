#include "atlas/index_builder.hpp"
#include "atlas/index_format.hpp"
#include "atlas/tokenizer.hpp"
#include "atlas/varbyte.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace atlas {
namespace {

// Postings for one term, stored column-wise to keep memory overhead low.
struct TermPostings {
    std::vector<uint32_t> docids;
    std::vector<uint32_t> freqs;
    std::vector<uint32_t> positions;  // concatenated, grouped per document
};

// Only large lists earn skip pointers; short lists scan faster than they seek.
constexpr uint32_t kSkipThreshold = 16;

void write_file(const std::string& path, const void* data, size_t bytes) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open for write: " + path);
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
}

// Encode one term's postings block (skip table + compressed docs region).
std::vector<uint8_t> encode_block(const TermPostings& tp) {
    const uint32_t df = static_cast<uint32_t>(tp.docids.size());

    std::vector<uint8_t> region;
    std::vector<size_t> start(df);
    uint32_t prev_doc = 0;
    size_t pi = 0;
    for (uint32_t i = 0; i < df; ++i) {
        start[i] = region.size();
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

    // Skip checkpoints every ~sqrt(df) documents.
    std::vector<std::pair<uint32_t, size_t>> skips;
    if (df >= kSkipThreshold) {
        const uint32_t step = std::max<uint32_t>(1, static_cast<uint32_t>(std::sqrt(df)));
        for (uint32_t i = step; i < df; i += step) {
            skips.emplace_back(tp.docids[i - 1], start[i]);
        }
    }

    std::vector<uint8_t> block;
    vbyte_encode(skips.size(), block);
    for (const auto& s : skips) {
        vbyte_encode(s.first, block);
        vbyte_encode(s.second, block);
    }
    block.insert(block.end(), region.begin(), region.end());
    return block;
}

}  // namespace

BuildStats build_index(const std::string& tsv_path, const std::string& out_dir) {
    const auto t0 = std::chrono::steady_clock::now();
    mkdir(out_dir.c_str(), 0755);

    std::ifstream in(tsv_path);
    if (!in) throw std::runtime_error("cannot open corpus: " + tsv_path);

    std::unordered_map<std::string, uint32_t> term_ids;
    std::vector<TermPostings> postings;
    std::vector<std::string> id_to_term;

    // Document metadata, collected as we stream the corpus.
    std::string docs_str;
    std::vector<DocEntry> docs;

    std::unordered_map<uint32_t, std::vector<uint32_t>> doc_terms;  // reused per doc
    std::string line;
    uint64_t total_tokens = 0;

    while (std::getline(in, line)) {
        const auto tab1 = line.find('\t');
        const auto tab2 = tab1 == std::string::npos ? std::string::npos : line.find('\t', tab1 + 1);
        std::string title = tab1 == std::string::npos ? "" : line.substr(0, tab1);
        std::string url = (tab1 == std::string::npos || tab2 == std::string::npos)
                              ? "" : line.substr(tab1 + 1, tab2 - tab1 - 1);
        std::string text = tab2 == std::string::npos ? line : line.substr(tab2 + 1);

        // Title text is searchable too, so index it alongside the body.
        const auto tokens = tokenize(title + " " + text);

        DocEntry de{};
        de.length = static_cast<uint32_t>(tokens.size());
        de.title_offset = docs_str.size();
        de.title_len = static_cast<uint32_t>(title.size());
        docs_str += title;
        de.url_offset = docs_str.size();
        de.url_len = static_cast<uint32_t>(url.size());
        docs_str += url;
        docs.push_back(de);

        const uint32_t docid = static_cast<uint32_t>(docs.size() - 1);
        total_tokens += tokens.size();

        doc_terms.clear();
        for (uint32_t pos = 0; pos < tokens.size(); ++pos) {
            auto it = term_ids.find(tokens[pos]);
            uint32_t tid;
            if (it == term_ids.end()) {
                tid = static_cast<uint32_t>(id_to_term.size());
                term_ids.emplace(tokens[pos], tid);
                id_to_term.push_back(tokens[pos]);
                postings.emplace_back();
            } else {
                tid = it->second;
            }
            doc_terms[tid].push_back(pos);
        }
        for (auto& [tid, plist] : doc_terms) {
            TermPostings& tp = postings[tid];
            tp.docids.push_back(docid);
            tp.freqs.push_back(static_cast<uint32_t>(plist.size()));
            tp.positions.insert(tp.positions.end(), plist.begin(), plist.end());
        }
    }

    const uint64_t num_docs = docs.size();
    const uint64_t num_terms = id_to_term.size();

    // Emit terms in sorted order so the reader can binary-search the dictionary.
    std::vector<uint32_t> order(num_terms);
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(),
              [&](uint32_t a, uint32_t b) { return id_to_term[a] < id_to_term[b]; });

    std::vector<uint8_t> postings_blob;
    std::string dict_str;
    std::vector<TermEntry> dict;
    dict.reserve(num_terms);
    uint64_t naive_bytes = 0;

    for (uint32_t tid : order) {
        const TermPostings& tp = postings[tid];
        const std::vector<uint8_t> block = encode_block(tp);

        TermEntry te{};
        te.str_offset = dict_str.size();
        te.str_len = static_cast<uint32_t>(id_to_term[tid].size());
        te.post_offset = postings_blob.size();
        te.post_len = static_cast<uint32_t>(block.size());
        te.doc_freq = static_cast<uint32_t>(tp.docids.size());
        dict.push_back(te);

        dict_str += id_to_term[tid];
        postings_blob.insert(postings_blob.end(), block.begin(), block.end());

        // Naive baseline: one fixed 4-byte int per docid, tf, and position.
        naive_bytes += 4ull * (2ull * tp.docids.size() + tp.positions.size());
    }

    // dict.bin
    {
        DictHeader h{kDictMagic, num_terms};
        std::vector<uint8_t> buf(sizeof(h) + dict.size() * sizeof(TermEntry));
        std::memcpy(buf.data(), &h, sizeof(h));
        std::memcpy(buf.data() + sizeof(h), dict.data(), dict.size() * sizeof(TermEntry));
        write_file(out_dir + "/dict.bin", buf.data(), buf.size());
    }
    write_file(out_dir + "/dict.str", dict_str.data(), dict_str.size());
    write_file(out_dir + "/postings.bin", postings_blob.data(), postings_blob.size());

    // docs.bin
    {
        DocsHeader h{};
        h.magic = kDocsMagic;
        h.num_docs = num_docs;
        h.avg_doc_len = num_docs ? static_cast<double>(total_tokens) / num_docs : 0.0;
        h.total_tokens = total_tokens;
        std::vector<uint8_t> buf(sizeof(h) + docs.size() * sizeof(DocEntry));
        std::memcpy(buf.data(), &h, sizeof(h));
        std::memcpy(buf.data() + sizeof(h), docs.data(), docs.size() * sizeof(DocEntry));
        write_file(out_dir + "/docs.bin", buf.data(), buf.size());
    }
    write_file(out_dir + "/docs.str", docs_str.data(), docs_str.size());

    const auto t1 = std::chrono::steady_clock::now();
    BuildStats st;
    st.num_docs = num_docs;
    st.num_terms = num_terms;
    st.total_tokens = total_tokens;
    st.postings_bytes = postings_blob.size();
    st.naive_bytes = naive_bytes;
    st.build_seconds = std::chrono::duration<double>(t1 - t0).count();
    return st;
}

}  // namespace atlas
