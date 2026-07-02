# atlas

A full-text search engine written in **C++** with a **Python/Flask** front end.
atlas builds a compressed inverted index over a document corpus and answers
ranked boolean and phrase queries in a few milliseconds. It has been built and
benchmarked end-to-end over **500,000 real English Wikipedia articles** (see
[Results](#results)).

It is a from-scratch implementation of the machinery behind a search engine —
tokenization, an inverted index, positional postings, variable-byte
compression, skip pointers, BM25 relevance scoring, a boolean query parser, and
a memory-mapped on-disk format — with no search libraries used.

```
┌──────────────┐   pybind11    ┌───────────────────────────────────────────┐
│  Flask API   │ ───────────►  │              C++ core (atlas)             │
│  + web UI    │               │                                           │
└──────────────┘               │  query parser → boolean/phrase eval →     │
                               │  BM25 ranking over a memory-mapped index  │
                               └───────────────────────────────────────────┘
                                        ▲
                                        │ mmap (no parse on open)
                               ┌────────┴────────┐
                               │  on-disk index  │  dict · postings · docs
                               └─────────────────┘
```

## Highlights

- **Inverted index** with **positional postings** — supports exact phrase
  queries, not just bag-of-words matching.
- **BM25** relevance scoring (`k1=1.2`, `b=0.75`) for ranked results.
- **Boolean query language**: `AND`, `OR`, `NOT`, quoted `"phrases"`,
  parentheses, and implicit-AND between adjacent terms, via a hand-written
  recursive-descent parser.
- **Variable-byte compression + delta (gap) encoding** of doc ids and
  positions — **~60–75% smaller** than a naive fixed-width layout.
- **Skip pointers** on long postings lists so `AND`-intersection can leap over
  non-matching documents instead of scanning them.
- **Memory-mapped index format**: opening an index only `mmap()`s a handful of
  flat files, so **cold start is sub-millisecond** rather than paying to parse
  the corpus.
- **Flask API + minimal web UI** for interactive querying.

## Results

Measured on **500,000 English Wikipedia articles** (streamed from the enwiki
CirrusSearch content dump by `download_wikipedia.py`), single core on an Apple
M-series laptop. Corpus: 37.6M tokens, 526k unique terms.

| Metric                          | Value                            |
| ------------------------------- | -------------------------------- |
| Documents indexed               | **500,000** (real Wikipedia)     |
| Cold-start index open (mmap)    | **~1.2 ms**                      |
| Query latency (median / p95)    | **0.61 ms / 13.9 ms**            |
| Throughput (mixed query set)    | **~580 queries/sec**             |
| Compressed index size           | **102 MB** (vs. 354 MB naive)    |
| Index size vs. naive layout     | **71.3% smaller**                |
| Build time                      | **6.7 s**                        |

Reproduce with the [real-corpus workflow](#using-real-wikipedia-data) below,
then `./build/atlas bench index_en data/queries_wiki.txt`. A smaller, fully
offline `make sample` path (synthetic corpus) reproduces the same shape without
any download.

## Quick start

Requires a C++17 compiler, CMake ≥ 3.16, and Python ≥ 3.9.

```bash
# 1. Create a virtualenv and install Python deps (flask, pybind11)
make venv

# 2. Build the C++ core, CLI, tests, and the pybind11 module
make build
make test

# 3. Generate a sample corpus and build an index over it
make sample          # writes data/sample.tsv (50k docs)
make index           # writes ./index/

# 4. Query from the command line...
./build/atlas search index "brown AND fox" 10
./build/atlas search index '"quick brown"' 10

# 5. ...or launch the web UI at http://127.0.0.1:5000
make serve
```

## Using real Wikipedia data

`make sample` produces synthetic text so everything runs offline. To index real
Wikipedia, stream a CirrusSearch content dump into the TSV format and build over
it. (Wikimedia retired the old abstracts XML dump; the CirrusSearch dumps are
the current source of clean per-article text, and each article's lead paragraph
`opening_text` serves as its abstract.)

```bash
# English Wikipedia — streams the 43 GB dump and stops at --limit documents
# (500k took ~25 min on a home connection; the dump is bandwidth-bound).
.venv/bin/python python/scripts/download_wikipedia.py --wiki enwiki --limit 500000 --out data/wiki_en.tsv

# ...or Simple English Wikipedia — a complete ~278k-article corpus, ~636 MB.
.venv/bin/python python/scripts/download_wikipedia.py --wiki simplewiki --out data/wiki_en.tsv

make index CORPUS=data/wiki_en.tsv INDEX_DIR=index_en
INDEX_DIR=index_en make serve
```

The downloader stream-parses the gzipped NDJSON (skipping each article's full
`text` field), so memory stays flat regardless of how many documents you pull.

## Query syntax

| Query                          | Meaning                                        |
| ------------------------------ | ---------------------------------------------- |
| `brown fox`                    | both terms (adjacent words imply `AND`)        |
| `brown AND fox`                | both terms                                     |
| `cats OR dogs`                 | either term                                    |
| `brown AND NOT fox`            | `brown`, excluding docs containing `fox`       |
| `"quick brown fox"`            | exact phrase, terms consecutive and in order   |
| `(cats OR dogs) AND NOT lazy`  | grouping with parentheses                      |

Precedence, tightest first: `NOT`, `AND`, `OR`.

## Architecture

### Build pipeline (`atlas build`)

1. Each corpus line (`title <TAB> url <TAB> text`) becomes one document; the
   line number is its doc id.
2. Text is tokenized to lowercase alphanumeric terms (title included).
3. Postings accumulate in memory as `term → [(docid, term-freq, positions…)]`.
4. Terms are sorted lexicographically and written out; each postings list is
   gap-encoded, varbyte-compressed, and given skip checkpoints.

### On-disk index format

An index is a directory of flat, memory-mappable files:

| File           | Contents                                                     |
| -------------- | ------------------------------------------------------------ |
| `dict.bin`     | header + `TermEntry[]`, sorted for binary search             |
| `dict.str`     | concatenated term strings                                    |
| `postings.bin` | one compressed postings block per term                       |
| `docs.bin`     | header (N, avg length) + `DocEntry[]` (per-doc length, refs) |
| `docs.str`     | concatenated document titles and URLs                        |

A postings block, with every integer varbyte-coded:

```
num_skips
skip_entries[num_skips] = { checkpoint_docid, byte_offset }
docs_region:
  per document:  docid_gap, term_freq, position_gap[term_freq]
```

Doc ids and positions are stored as **gaps** (differences), which are small and
therefore cheap in varbyte. **Skip entries** are placed every ~√(doc-freq)
documents; each says "resuming decode at this byte offset, the next gap is
relative to this checkpoint doc id," letting the intersection algorithm jump
ahead.

### Query pipeline (`atlas search`)

1. **Parse** the query into an AST (recursive-descent parser).
2. **Evaluate** the boolean structure to a sorted list of candidate doc ids:
   - terms → postings scan; `AND` → skip-accelerated k-way intersection;
     `OR` → merge-union; `NOT` → complement; phrases → positional intersection.
3. **Rank** candidates with BM25 over the query's scoring terms and return the
   top *k* via a partial sort.

### Why memory mapping

Opening an index does not deserialize anything — it `mmap()`s the files and
reads a couple of fixed headers. The OS page cache faults in only the postings
actually touched by a query. This keeps cold start in the sub-millisecond range
and lets multiple processes share one physical copy of the index.

## Benchmarks

```bash
make sample                       # or use data/wiki.tsv
make index
make bench                        # runs data/queries.txt, reports latency + qps
```

`atlas build` also prints the compression ratio against a naive baseline (one
fixed 4-byte integer per doc id, term frequency, and position — no gaps, no
varbyte):

```
  documents      : 500000
  unique terms   : 525906
  postings size  : 101.77 MB (compressed)
  naive size     : 354.26 MB (fixed 4-byte ints)
  index shrink   : 71.3% smaller than naive
```

## Project layout

```
cpp/
  include/atlas/     public headers (varbyte, tokenizer, index format, ...)
  src/               tokenizer, index_builder, index (mmap reader),
                     query_parser, searcher (BM25 + boolean/phrase eval)
  tools/atlas_cli.cpp   build / search / bench CLI
  bindings/pyatlas.cpp  pybind11 module (atlas_native)
  tests/test_atlas.cpp  self-contained test suite
python/
  atlas_search/      Flask app, HTML template, CSS
  scripts/           make_sample.py, download_wikipedia.py
CMakeLists.txt       builds the library, CLI, tests, and Python module
Makefile             venv / build / index / bench / serve shortcuts
```

## Testing

`make test` builds and runs the C++ suite, covering the varbyte round-trip, the
tokenizer, boolean/phrase/NOT evaluation, BM25 result sets, and skip-pointer
traversal over a long postings list.

## Design notes & limitations

- The index builder holds postings in memory, so peak build RAM scales with the
  corpus. A production system would spill and external-merge; that is the
  natural next step here.
- No stemming or stop-word removal — positions map 1:1 to source tokens, which
  keeps phrase matching exact. Both are straightforward to layer in.
- Single-node, read-only after build. Incremental updates would need segment
  merging (à la Lucene).

## License

MIT — see [LICENSE](LICENSE).
