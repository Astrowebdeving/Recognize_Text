#include "vision/layout/ReaderFormatter.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace loupe {

namespace {
float coordinate(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}

float left(const OcrToken& token) {
    return std::min({coordinate(token.sourceQuad.x1), coordinate(token.sourceQuad.x2),
                     coordinate(token.sourceQuad.x3), coordinate(token.sourceQuad.x4)});
}

float right(const OcrToken& token) {
    return std::max({coordinate(token.sourceQuad.x1), coordinate(token.sourceQuad.x2),
                     coordinate(token.sourceQuad.x3), coordinate(token.sourceQuad.x4)});
}
} // namespace

std::string ReaderFormatter::format(const OcrResult& result) const {
    std::map<uint32_t, std::vector<const OcrToken*>> byLine;
    for (const auto& token : result.tokens) byLine[token.lineIndex].push_back(&token);

    std::ostringstream output;
    bool firstLine = true;
    for (auto& [lineIndex, tokens] : byLine) {
        (void)lineIndex;
        std::sort(tokens.begin(), tokens.end(), [](const auto* a, const auto* b) {
            return left(*a) < left(*b);
        });
        if (!firstLine) output << '\n';
        firstLine = false;
        float previousRight = 0.0F;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) {
                const auto gap = left(*tokens[i]) - previousRight;
                const auto spaces = std::clamp(static_cast<int>(std::lround(gap * 30.0F)), 1, 8);
                output << std::string(static_cast<size_t>(spaces), ' ');
            }
            output << tokens[i]->text;
            previousRight = right(*tokens[i]);
        }
    }
    return output.str();
}

bool ReaderFormatter::prefersOverlay(const OcrResult& result) const noexcept {
    if (result.lines.size() > 3 || result.tokens.size() > 16) return true;
    size_t alignedColumns = 0;
    for (const auto& token : result.tokens) {
        if (token.columnIndex.has_value()) ++alignedColumns;
    }
    return result.lines.size() > 1 && alignedColumns > result.tokens.size() / 2;
}

} // namespace loupe
