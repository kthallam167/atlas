#pragma once
// Recursive-descent parser for the query language:
//   term            a bare word
//   "a b c"         a phrase (positional match)
//   x AND y         both (also the implicit operator between adjacent terms)
//   x OR y          either
//   NOT x           exclude
//   ( ... )         grouping
// Precedence, tightest first: NOT, AND, OR.
#include <memory>
#include <string>
#include <vector>

namespace atlas {

enum class NodeType { Term, Phrase, And, Or, Not };

struct QueryNode {
    NodeType type;
    std::string term;                  // Term
    std::vector<std::string> phrase;   // Phrase
    std::vector<std::unique_ptr<QueryNode>> children;  // And / Or
    std::unique_ptr<QueryNode> child;  // Not
};

// Returns nullptr for an empty query. Throws std::runtime_error on malformed
// input (e.g. unbalanced parentheses).
std::unique_ptr<QueryNode> parse_query(const std::string& input);

}  // namespace atlas
