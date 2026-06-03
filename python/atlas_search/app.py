"""Flask API that serves ranked search results from the C++ core.

    INDEX_DIR=index python -m atlas_search.app        # or: make serve

The atlas_native module is built by CMake into ./build; we add that to the
import path so no separate install step is required.
"""
import os
import sys
import time
from pathlib import Path

from flask import Flask, jsonify, render_template, request

# Make the compiled atlas_native module importable from the CMake build dir.
_REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "build"))

import atlas_native  # noqa: E402

INDEX_DIR = os.environ.get("INDEX_DIR", str(_REPO_ROOT / "index"))

app = Flask(__name__)
engine = atlas_native.Engine(INDEX_DIR)
print(f"loaded index '{INDEX_DIR}' with {engine.num_docs()} documents")


@app.route("/")
def home():
    return render_template("index.html", num_docs=engine.num_docs())


@app.route("/api/search")
def search():
    query = request.args.get("q", "").strip()
    try:
        k = min(max(int(request.args.get("k", 10)), 1), 50)
    except ValueError:
        k = 10
    if not query:
        return jsonify({"query": query, "count": 0, "took_ms": 0.0, "results": []})

    start = time.perf_counter()
    results = engine.search(query, k)
    took_ms = (time.perf_counter() - start) * 1000.0
    return jsonify(
        {
            "query": query,
            "count": len(results),
            "took_ms": round(took_ms, 3),
            "results": results,
        }
    )


def main():
    app.run(host="127.0.0.1", port=int(os.environ.get("PORT", 5000)), debug=False)


if __name__ == "__main__":
    main()
