#include "atlas/index.hpp"
#include "atlas/varbyte.hpp"

#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace atlas {

// ---- Mmap -----------------------------------------------------------------

Mmap::Mmap(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) throw std::runtime_error("cannot open: " + path);
    struct stat st{};
    if (::fstat(fd_, &st) != 0) throw std::runtime_error("fstat failed: " + path);
    size_ = static_cast<size_t>(st.st_size);
    if (size_ > 0) {
        void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (p == MAP_FAILED) throw std::runtime_error("mmap failed: " + path);
        data_ = static_cast<const uint8_t*>(p);
    }
}

Mmap::~Mmap() {
    if (data_) ::munmap(const_cast<uint8_t*>(data_), size_);
    if (fd_ >= 0) ::close(fd_);
}

Mmap::Mmap(Mmap&& o) noexcept : data_(o.data_), size_(o.size_), fd_(o.fd_) {
    o.data_ = nullptr; o.size_ = 0; o.fd_ = -1;
}

Mmap& Mmap::operator=(Mmap&& o) noexcept {
    if (this != &o) {
        if (data_) ::munmap(const_cast<uint8_t*>(data_), size_);
        if (fd_ >= 0) ::close(fd_);
        data_ = o.data_; size_ = o.size_; fd_ = o.fd_;
        o.data_ = nullptr; o.size_ = 0; o.fd_ = -1;
    }
    return *this;
}

// ---- PostingsIterator -----------------------------------------------------

PostingsIterator::PostingsIterator(const uint8_t* block, uint32_t block_len, uint32_t doc_freq)
    : doc_freq_(doc_freq) {
    size_t p = 0;
    const uint64_t num_skips = vbyte_decode(block, p);
    skips_.reserve(num_skips);
    for (uint64_t i = 0; i < num_skips; ++i) {
        const uint32_t docid = static_cast<uint32_t>(vbyte_decode(block, p));
        const uint64_t off = vbyte_decode(block, p);
        skips_.emplace_back(docid, off);
    }
    region_ = block + p;
    region_size_ = block_len - p;
    cur_pos_ = 0;
    cur_doc_ = 0;
    decode_current();
}

void PostingsIterator::decode_current() {
    if (cur_pos_ >= region_size_) { cur_doc_ = kNoDoc; return; }
    cur_doc_ += static_cast<uint32_t>(vbyte_decode(region_, cur_pos_));
    cur_tf_ = static_cast<uint32_t>(vbyte_decode(region_, cur_pos_));
    pos_start_ = cur_pos_;  // cur_pos_ stays here; next() skips the positions
}

void PostingsIterator::next() {
    if (at_end()) return;
    size_t p = pos_start_;
    for (uint32_t j = 0; j < cur_tf_; ++j) vbyte_decode(region_, p);
    cur_pos_ = p;
    decode_current();
}

void PostingsIterator::advance(uint32_t target) {
    if (at_end() || cur_doc_ >= target) return;

    // Leap to the furthest checkpoint before `target` that is ahead of us.
    for (const auto& s : skips_) {
        if (s.first < target && s.second > cur_pos_) {
            cur_doc_ = s.first;
            cur_pos_ = s.second;
            decode_current();
        } else if (s.first >= target) {
            break;  // skips are sorted; no closer checkpoint exists
        }
    }
    while (!at_end() && cur_doc_ < target) next();
}

std::vector<uint32_t> PostingsIterator::positions() const {
    std::vector<uint32_t> out;
    out.reserve(cur_tf_);
    size_t p = pos_start_;
    uint32_t prev = 0;
    for (uint32_t j = 0; j < cur_tf_; ++j) {
        prev += static_cast<uint32_t>(vbyte_decode(region_, p));
        out.push_back(prev);
    }
    return out;
}

// ---- Index ----------------------------------------------------------------

Index::Index(const std::string& dir)
    : dict_(dir + "/dict.bin"),
      dict_str_(dir + "/dict.str"),
      postings_(dir + "/postings.bin"),
      docs_(dir + "/docs.bin"),
      docs_str_(dir + "/docs.str") {
    if (dict_header().magic != kDictMagic) throw std::runtime_error("bad dict magic");
    if (docs_header().magic != kDocsMagic) throw std::runtime_error("bad docs magic");
}

const DictHeader& Index::dict_header() const {
    return *reinterpret_cast<const DictHeader*>(dict_.data());
}
const DocsHeader& Index::docs_header() const {
    return *reinterpret_cast<const DocsHeader*>(docs_.data());
}
const TermEntry* Index::term_entries() const {
    return reinterpret_cast<const TermEntry*>(dict_.data() + sizeof(DictHeader));
}
const DocEntry* Index::doc_entries() const {
    return reinterpret_cast<const DocEntry*>(docs_.data() + sizeof(DocsHeader));
}

const TermEntry* Index::find_term(const std::string& term) const {
    const TermEntry* entries = term_entries();
    const char* strs = reinterpret_cast<const char*>(dict_str_.data());
    int64_t lo = 0, hi = static_cast<int64_t>(dict_header().num_terms) - 1;
    while (lo <= hi) {
        const int64_t mid = (lo + hi) / 2;
        const TermEntry& e = entries[mid];
        const int cmp = term.compare(0, term.size(),
                                     strs + e.str_offset, e.str_len);
        if (cmp == 0) return &e;
        if (cmp < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return nullptr;
}

PostingsIterator Index::postings(const TermEntry& te) const {
    return PostingsIterator(postings_.data() + te.post_offset, te.post_len, te.doc_freq);
}

uint32_t Index::doc_length(uint32_t docid) const {
    return doc_entries()[docid].length;
}
// Optimized zero-copy doc string extraction
std::string Index::doc_title(uint32_t docid) const {
    const DocEntry& d = doc_entries()[docid];
    return std::string(reinterpret_cast<const char*>(docs_str_.data()) + d.title_offset, d.title_len);
}
std::string Index::doc_url(uint32_t docid) const {
    const DocEntry& d = doc_entries()[docid];
    return std::string(reinterpret_cast<const char*>(docs_str_.data()) + d.url_offset, d.url_len);
}

}  // namespace atlas
