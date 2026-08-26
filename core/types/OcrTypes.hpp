#pragma once

#include "core/types/CaptureTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace loupe {

struct NormalizedQuad {
    float x1{}, y1{};
    float x2{}, y2{};
    float x3{}, y3{};
    float x4{}, y4{};
};

struct OcrCharacter {
    std::string utf8;
    float confidence{};
    NormalizedQuad sourceQuad;
};

struct OcrToken {
    std::string text;
    float confidence{};
    NormalizedQuad sourceQuad;
    std::vector<OcrCharacter> characters;
    bool numericLike{};
    uint32_t lineIndex{};
    std::optional<uint32_t> columnIndex;
};

struct OcrLine {
    std::vector<size_t> tokenIndices;
    NormalizedQuad sourceQuad;
};

struct OcrResult {
    GenerationId generation{};
    std::vector<OcrToken> tokens;
    std::vector<OcrLine> lines;
    float overallConfidence{};
    std::string engine;
    std::string error;
};

enum class ConfidenceBand {
    Unknown,
    Low,
    Medium,
    High
};

[[nodiscard]] inline ConfidenceBand confidenceBand(float confidence) noexcept {
    if (!std::isfinite(confidence) || confidence <= 0.0F) return ConfidenceBand::Unknown;
    if (confidence < 0.70F) return ConfidenceBand::Low;
    if (confidence < 0.90F) return ConfidenceBand::Medium;
    return ConfidenceBand::High;
}

enum class RoiKind {
    Unknown,
    Token,
    Line,
    MultiLine,
    TableLike
};

} // namespace loupe
