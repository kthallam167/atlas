#include "atlas/tokenizer.hpp"
#include <cctype>

namespace atlas {

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string cur;
    cur.reserve(24);
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            cur.push_back(static_cast<char>(std::tolower(c)));
        } else if (!cur.empty()) {
            tokens.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

}  // namespace atlas
