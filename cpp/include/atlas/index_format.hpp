#pragma once
// On-disk layout shared by the builder (writer) and the mmap reader.
//
// An index is a directory of flat files, each memory-mappable as-is:
//
//   dict.bin      header + TermEntry[]  (terms sorted lexicographically)
//   dict.str      concatenated term bytes referenced by TermEntry
//   postings.bin  one compressed postings block per term (see below)
//   docs.bin      header + DocEntry[]   (per-document metadata)
//   docs.str      concatenated title/url bytes referenced by DocEntry
//
// Postings block for one term (all integers varbyte-coded):
//   num_skips
//   skip_entries[num_skips] = { checkpoint_docid, byte_offset }
//   docs_region:
//     per document: docid_gap, term_freq, position_gap[term_freq]
// A skip entry says: "resuming decode at docs_region+byte_offset, the next
// docid_gap is relative to checkpoint_docid." Skips are placed every
// ~sqrt(doc_freq) documents so AND-intersection can leap over non-matches.
#include <cstdint>

namespace atlas {

constexpr uint64_t kDictMagic = 0x314B53494C544101ULL;  // "ATLIS K1"
constexpr uint64_t kDocsMagic = 0x314B53434F444101ULL;  // "ATLDOC K1"

#pragma pack(push, 1)

struct DictHeader {
    uint64_t magic;
    uint64_t num_terms;
};

struct TermEntry {
    uint64_t str_offset;    // into dict.str
    uint32_t str_len;
    uint64_t post_offset;   // into postings.bin
    uint32_t post_len;      // bytes in postings block
    uint32_t doc_freq;
};

struct DocsHeader {
    uint64_t magic;
    uint64_t num_docs;
    double   avg_doc_len;   // average token count, for BM25
    uint64_t total_tokens;
};

struct DocEntry {
    uint32_t length;        // token count of the document
    uint64_t title_offset;  // into docs.str
    uint32_t title_len;
    uint64_t url_offset;    // into docs.str
    uint32_t url_len;
};

#pragma pack(pop)

}  // namespace atlas
