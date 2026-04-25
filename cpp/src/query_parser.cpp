#include "atlas/query_parser.hpp"
#include "atlas/tokenizer.hpp"

#include <cctype>
#include <stdexcept>

namespace atlas {
namespace {

enum class Tok { Word, Phrase, And, Or, Not, LParen, RParen, End };

struct Lexeme {
    Tok kind;
    std::string text;             // Word
    std::vector<std::string> phrase;  // Phrase
};

// Split the raw query into lexemes: operators, parentheses, quoted phrases,
// and words. Operators are matched case-insensitively.
std::vector<Lexeme> lex(const std::string& in) {
    std::vector<Lexeme> out;
    size_t i = 0;
    while (i < in.size()) {
        const char c = in[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '(') { out.push_back({Tok::LParen, "", {}}); ++i; continue; }
        if (c == ')') { out.push_back({Tok::RParen, "", {}}); ++i; continue; }
        if (c == '"') {
            const size_t end = in.find('"', i + 1);
            const std::string body = in.substr(i + 1, (end == std::string::npos ? in.size() : end) - i - 1);
            out.push_back({Tok::Phrase, "", tokenize(body)});
            i = (end == std::string::npos) ? in.size() : end + 1;
            continue;
        }
        size_t j = i;
        while (j < in.size() && !std::isspace(static_cast<unsigned char>(in[j]))
               && in[j] != '(' && in[j] != ')' && in[j] != '"') ++j;
        std::string word = in.substr(i, j - i);
        i = j;
        std::string upper;
        for (char ch : word) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        if (upper == "AND") out.push_back({Tok::And, "", {}});
        else if (upper == "OR") out.push_back({Tok::Or, "", {}});
        else if (upper == "NOT") out.push_back({Tok::Not, "", {}});
        else {
            // A single query word may still contain punctuation; normalize it.
            const auto toks = tokenize(word);
            if (toks.empty()) continue;
            if (toks.size() == 1) out.push_back({Tok::Word, toks[0], {}});
            else out.push_back({Tok::Phrase, "", toks});  // e.g. "e-mail" -> phrase
        }
    }
    out.push_back({Tok::End, "", {}});
    return out;
}

class Parser {
public:
    explicit Parser(std::vector<Lexeme> toks) : toks_(std::move(toks)) {}

    std::unique_ptr<QueryNode> parse() {
        if (peek().kind == Tok::End) return nullptr;
        auto node = parse_or();
        if (peek().kind != Tok::End) throw std::runtime_error("unexpected token in query");
        return node;
    }

private:
    const Lexeme& peek() const { return toks_[pos_]; }
    const Lexeme& take() { return toks_[pos_++]; }

    static bool starts_atom(Tok t) {
        return t == Tok::Word || t == Tok::Phrase || t == Tok::LParen || t == Tok::Not;
    }

    std::unique_ptr<QueryNode> parse_or() {
        auto left = parse_and();
        while (peek().kind == Tok::Or) {
            take();
            auto node = std::make_unique<QueryNode>();
            node->type = NodeType::Or;
            node->children.push_back(std::move(left));
            node->children.push_back(parse_and());
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<QueryNode> parse_and() {
        auto left = parse_not();
        // Implicit AND: another atom (not OR / close-paren) continues the group.
        while (peek().kind == Tok::And || starts_atom(peek().kind)) {
            if (peek().kind == Tok::And) take();
            auto node = std::make_unique<QueryNode>();
            node->type = NodeType::And;
            node->children.push_back(std::move(left));
            node->children.push_back(parse_not());
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<QueryNode> parse_not() {
        if (peek().kind == Tok::Not) {
            take();
            auto node = std::make_unique<QueryNode>();
            node->type = NodeType::Not;
            node->child = parse_not();
            return node;
        }
        return parse_atom();
    }

    std::unique_ptr<QueryNode> parse_atom() {
        const Lexeme& l = peek();
        if (l.kind == Tok::LParen) {
            take();
            auto inner = parse_or();
            if (peek().kind != Tok::RParen) throw std::runtime_error("unbalanced parentheses");
            take();
            return inner;
        }
        if (l.kind == Tok::Word) {
            take();
            auto node = std::make_unique<QueryNode>();
            node->type = NodeType::Term;
            node->term = l.text;
            return node;
        }
        if (l.kind == Tok::Phrase) {
            take();
            auto node = std::make_unique<QueryNode>();
            node->type = NodeType::Phrase;
            node->phrase = l.phrase;
            return node;
        }
        throw std::runtime_error("expected a term");
    }

    std::vector<Lexeme> toks_;
    size_t pos_ = 0;
};

}  // namespace

std::unique_ptr<QueryNode> parse_query(const std::string& input) {
    return Parser(lex(input)).parse();
}

}  // namespace atlas
