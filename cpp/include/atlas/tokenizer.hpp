#pragma once
// Text -> lowercase alphanumeric tokens. No stemming, so recorded positions
// line up exactly with the source text for phrase queries.
#include <string>
#include <vector>

namespace atlas {

std::vector<std::string> tokenize(const std::string& text);

}  // namespace atlas
