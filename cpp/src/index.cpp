#include "atlas/index.hpp"
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace atlas {

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

const TermEntry* Index::find_term(const std::string& term) const {
    const TermEntry* entries = term_entries();
    const char* strs = reinterpret_cast<const char*>(dict_str_.data());
    int64_t lo = 0, hi = static_cast<int64_t>(dict_header().num_terms) - 1;
    while (lo <= hi) {
        const int64_t mid = (lo + hi) / 2;
        const TermEntry& e = entries[mid];
        const int cmp = term.compare(0, term.size(), strs + e.str_offset, e.str_len);
        if (cmp == 0) return &e;
        if (cmp < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return nullptr;
}

}  // namespace atlas
