#pragma once
#include "atlas/index_format.hpp"
#include <cstdint>
#include <string>

namespace atlas {

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

class Index {
public:
    explicit Index(const std::string& dir);
    uint64_t num_docs() const { return docs_header().num_docs; }
    const TermEntry* find_term(const std::string& term) const;

private:
    const DictHeader& dict_header() const;
    const DocsHeader& docs_header() const;
    const TermEntry* term_entries() const;

    Mmap dict_, dict_str_, postings_, docs_, docs_str_;
};

}  // namespace atlas
