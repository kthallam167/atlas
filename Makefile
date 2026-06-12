# Convenience wrapper around CMake + the Python front end.
# Most targets assume a virtualenv at .venv (see `make venv`).

PY := .venv/bin/python
PIP := .venv/bin/pip
PYBIND_CMAKE := $(shell $(PY) -m pybind11 --cmakedir 2>/dev/null)

INDEX_DIR ?= index
CORPUS ?= data/sample.tsv

.PHONY: all venv build test sample index bench serve clean

all: build

venv:
	python3 -m venv .venv
	$(PIP) install -q --upgrade pip
	$(PIP) install -q -r python/requirements.txt

# Configure and compile the C++ core, CLI, tests, and pybind11 module.
build:
	cmake -S . -B build -DCMAKE_PREFIX_PATH="$(PYBIND_CMAKE)"
	cmake --build build -j

test: build
	./build/atlas_tests

# Generate a synthetic corpus for offline demos/benchmarks.
sample:
	$(PY) python/scripts/make_sample.py --docs 50000 --out $(CORPUS)

# Build the on-disk index from CORPUS.
index: build
	./build/atlas build $(CORPUS) $(INDEX_DIR)

bench: build
	./build/atlas bench $(INDEX_DIR) data/queries.txt

# Launch the Flask UI at http://127.0.0.1:5000
serve: build
	cd python && INDEX_DIR=../$(INDEX_DIR) ../$(PY) -m atlas_search.app

clean:
	rm -rf build index data/*.tsv
