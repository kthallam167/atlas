// Python bindings (module: atlas_native). The Flask app talks to the C++ core
// entirely through this thin layer.
#include "atlas/index.hpp"
#include "atlas/index_builder.hpp"
#include "atlas/searcher.hpp"

#include <memory>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

// Owns an Index plus its Searcher so Python holds a single handle.
class PyEngine {
public:
    explicit PyEngine(const std::string& dir)
        : index_(std::make_unique<atlas::Index>(dir)),
          searcher_(std::make_unique<atlas::Searcher>(*index_)) {}

    py::list search(const std::string& query, size_t k) const {
        py::list out;
        for (const auto& r : searcher_->search(query, k)) {
            py::dict d;
            d["docid"] = r.docid;
            d["score"] = r.score;
            d["title"] = r.title;
            d["url"] = r.url;
            out.append(std::move(d));
        }
        return out;
    }

    uint64_t num_docs() const { return index_->num_docs(); }

private:
    std::unique_ptr<atlas::Index> index_;
    std::unique_ptr<atlas::Searcher> searcher_;
};

PYBIND11_MODULE(atlas_native, m) {
    m.doc() = "atlas full-text search engine (C++ core)";

    m.def("build_index", [](const std::string& corpus, const std::string& out_dir) {
        const atlas::BuildStats s = atlas::build_index(corpus, out_dir);
        py::dict d;
        d["num_docs"] = s.num_docs;
        d["num_terms"] = s.num_terms;
        d["total_tokens"] = s.total_tokens;
        d["postings_bytes"] = s.postings_bytes;
        d["naive_bytes"] = s.naive_bytes;
        d["build_seconds"] = s.build_seconds;
        return d;
    }, py::arg("corpus"), py::arg("out_dir"));

    py::class_<PyEngine>(m, "Engine")
        .def(py::init<const std::string&>(), py::arg("index_dir"))
        .def("search", &PyEngine::search, py::arg("query"), py::arg("k") = 10)
        .def("num_docs", &PyEngine::num_docs);
}
