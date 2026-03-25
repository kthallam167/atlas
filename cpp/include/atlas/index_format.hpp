#pragma once
#include <cstdint>

namespace atlas {

#pragma pack(push, 1)

struct DictHeader {
    uint64_t magic;
    uint64_t num_terms;
};

struct TermEntry {
    uint64_t str_offset;
    uint32_t str_len;
    uint64_t post_offset;
    uint32_t post_len;
    uint32_t doc_freq;
};

struct DocsHeader {
    uint64_t magic;
    uint64_t num_docs;
    double   avg_doc_len;
    uint64_t total_tokens;
};

struct DocEntry {
    uint32_t length;
    uint64_t title_offset;
    uint32_t title_len;
    uint64_t url_offset;
    uint32_t url_len;
};

#pragma pack(pop)

}  // namespace atlas
