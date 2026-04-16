#pragma once
// Read-only, memory-mapped view of an on-disk index. Opening only maps files
// and reads headers, so cold start is dominated by a few mmap() calls rather
// than by parsing the corpus.
#include "atlas/index_format.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace atlas {

constexpr uint32_t kNoDoc = 0xFFFFFFFFu;  // iterator end sentinel

// RAII wrapper around a read-only mmap of one file.
class Mmap {
public:
    Mmap() = default;
    explicit Mmap(const std::string& path);
    ~Mmap();
    Mmap(Mmap&&) noexcept;
    Mmap& operator=(Mmap&&) noexcept;
    Mmap(const Mmap&) = delete;
    Mmap& operator=(const Mmap&) = delete;

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
};

// Cursor over one term's postings. Supports sequential next() and
// skip-accelerated advance() for AND-intersection.
class PostingsIterator {
public:
    PostingsIterator(const uint8_t* block, uint32_t block_len, uint32_t doc_freq);

    uint32_t docid() const { return cur_doc_; }
    uint32_t freq() const { return cur_tf_; }
    bool at_end() const { return cur_doc_ == kNoDoc; }
    uint32_t doc_freq() const { return doc_freq_; }

    void next();
    void advance(uint32_t target);          // to first doc with docid >= target
    std::vector<uint32_t> positions() const; // decode current doc's positions

private:
    void decode_current();

    const uint8_t* region_ = nullptr;  // docs region (after skip table)
    size_t region_size_ = 0;
    size_t cur_pos_ = 0;               // offset of current record in region
    size_t pos_start_ = 0;            // offset of current doc's positions
    uint32_t cur_doc_ = 0;
    uint32_t cur_tf_ = 0;
    uint32_t doc_freq_ = 0;
    std::vector<std::pair<uint32_t, uint64_t>> skips_;  // {checkpoint_docid, offset}
};

class Index {
public:
    explicit Index(const std::string& dir);

    uint64_t num_docs() const { return docs_header().num_docs; }
    double avg_doc_len() const { return docs_header().avg_doc_len; }

    // Returns nullptr if the term is absent.
    const TermEntry* find_term(const std::string& term) const;
    PostingsIterator postings(const TermEntry& te) const;

    uint32_t doc_length(uint32_t docid) const;
    std::string doc_title(uint32_t docid) const;
    std::string doc_url(uint32_t docid) const;

private:
    const DictHeader& dict_header() const;
    const DocsHeader& docs_header() const;
    const TermEntry* term_entries() const;
    const DocEntry* doc_entries() const;

    Mmap dict_, dict_str_, postings_, docs_, docs_str_;
};

}  // namespace atlas
