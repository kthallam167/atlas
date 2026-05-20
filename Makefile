PY := .venv/bin/python
PIP := .venv/bin/pip
PYBIND_CMAKE := $(shell $(PY) -m pybind11 --cmakedir 2>/dev/null)

INDEX_DIR ?= index
CORPUS ?= data/sample.tsv

.PHONY: all venv build test sample index bench clean

all: build

venv:
	python3 -m venv .venv
	$(PIP) install -q --upgrade pip
	$(PIP) install -q -r python/requirements.txt

build:
	cmake -S . -B build -DCMAKE_PREFIX_PATH="$(PYBIND_CMAKE)"
	cmake --build build -j

test: build
	./build/atlas_tests

sample:
	$(PY) python/scripts/make_sample.py --docs 50000 --out $(CORPUS)

index: build
	./build/atlas build $(CORPUS) $(INDEX_DIR)

bench: build
	./build/atlas bench $(INDEX_DIR) data/queries.txt

clean:
	rm -rf build index data/*.tsv
