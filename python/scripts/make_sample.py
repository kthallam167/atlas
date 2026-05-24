#!/usr/bin/env python3
"""Generate a synthetic corpus (title<TAB>url<TAB>text per line).

Useful for exercising and benchmarking the engine offline, without the multi-GB
Wikipedia dump. Vocabulary follows a Zipfian frequency so some terms have long
postings lists (which is where skip pointers and BM25 IDF matter).

    python scripts/make_sample.py --docs 50000 --out data/sample.tsv
"""
import argparse
import random

WORDS = (
    "search index query engine document term postings ranking relevance score "
    "vector matrix boolean phrase token corpus retrieval inverted compression "
    "wikipedia abstract article history science physics biology chemistry math "
    "computer system network protocol memory cache latency throughput cluster "
    "algorithm data structure graph tree heap sort merge hash table pointer "
    "language model neural network training gradient descent optimization loss "
    "ocean mountain river desert forest climate weather storm season planet star"
).split()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--docs", type=int, default=50000)
    ap.add_argument("--out", default="data/sample.tsv")
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    # Zipfian weights: word i chosen with probability ~ 1/(i+1).
    weights = [1.0 / (i + 1) for i in range(len(WORDS))]

    with open(args.out, "w", encoding="utf-8") as f:
        for i in range(args.docs):
            n = rng.randint(15, 40)
            body = rng.choices(WORDS, weights=weights, k=n)
            title = " ".join(w.capitalize() for w in body[:3])
            url = f"https://example.org/doc/{i}"
            f.write(f"{title}\t{url}\t{' '.join(body)}\n")

    print(f"wrote {args.docs} documents to {args.out}")


if __name__ == "__main__":
    main()
