#!/usr/bin/env python3
"""Stream a Wikipedia CirrusSearch content dump into atlas TSV format.

Wikimedia retired the old abstracts XML dump; the current source of clean,
per-article text is the CirrusSearch content dump — gzipped NDJSON where each
article is two lines: an index-action line, then the document. We stream it so
memory stays flat, take each article's lead paragraph (``opening_text``) as its
"abstract", and write one document per line:

    title <TAB> url <TAB> abstract

    # ~350k complete articles, ~636 MB download:
    python scripts/download_wikipedia.py --wiki simplewiki --limit 500000 --out data/wiki.tsv

    # English Wikipedia (43 GB dump; streaming stops once --limit is reached):
    python scripts/download_wikipedia.py --wiki enwiki --limit 500000 --out data/wiki.tsv
"""
import argparse
import gzip
import io
import json
import re
import sys
import urllib.request

INDEX = "https://dumps.wikimedia.org/other/cirrussearch/"
# Wikimedia requires a descriptive User-Agent with contact info on every request.
UA = "atlas-search/1.0 (https://github.com/; educational project)"


def _open(url: str, method: str = "GET"):
    req = urllib.request.Request(url, method=method, headers={"User-Agent": UA})
    return urllib.request.urlopen(req, timeout=120)


def latest_url(wiki: str) -> str:
    """Resolve the newest dated CirrusSearch content dump for `wiki`."""
    listing = _open(INDEX).read().decode("utf-8", "replace")
    dates = sorted(set(re.findall(r"(2\d{7})/", listing)))
    for date in reversed(dates):
        name = f"{wiki}-{date}-cirrussearch-content.json.gz"
        url = f"{INDEX}{date}/{name}"
        try:
            _open(url, method="HEAD")
            return url
        except Exception:
            continue
    raise SystemExit(f"no CirrusSearch content dump found for {wiki}")


def clean(text: str) -> str:
    return " ".join((text or "").split())


def parse_doc(line: bytes):
    """Parse a CirrusSearch doc line (bytes), skipping the large ``text`` field.

    ``title`` and ``opening_text`` always precede ``text`` in the record, so we
    truncate the line before ``text`` and close the object — this avoids parsing
    each article's full body (the dominant cost). Falls back to a full parse if
    the truncation doesn't yield a usable record.
    """
    cut = line.find(b',"text":"')
    if cut != -1:
        try:
            doc = json.loads(line[:cut] + b"}")
            if doc.get("title"):
                return doc
        except json.JSONDecodeError:
            pass
    return json.loads(line)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--wiki", default="simplewiki", help="e.g. simplewiki, enwiki")
    ap.add_argument("--limit", type=int, default=500000)
    ap.add_argument("--out", default="data/wiki.tsv")
    ap.add_argument("--url", default=None, help="override the dump URL")
    args = ap.parse_args()

    url = args.url or latest_url(args.wiki)
    print(f"streaming {url}")

    with _open(url) as resp:
        stream = io.BufferedReader(gzip.GzipFile(fileobj=resp))
        written = 0
        expect_doc = False
        with open(args.out, "w", encoding="utf-8") as out:
            for raw in stream:
                # NDJSON: index-action line, then the document line, alternating.
                if not expect_doc:
                    expect_doc = True
                    continue
                expect_doc = False
                try:
                    doc = parse_doc(raw)
                except json.JSONDecodeError:
                    continue
                title = clean(doc.get("title", ""))
                abstract = clean(doc.get("opening_text") or doc.get("text", ""))[:600]
                if not title or not abstract:
                    continue
                url_slug = title.replace(" ", "_")
                page_url = f"https://en.wikipedia.org/wiki/{url_slug}"
                out.write(f"{title}\t{page_url}\t{abstract}\n")
                written += 1
                if written % 25000 == 0:
                    print(f"  {written} documents", flush=True)
                if written >= args.limit:
                    break

    print(f"wrote {written} documents to {args.out}")


if __name__ == "__main__":
    main()
