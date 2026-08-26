#pragma once

#include "core/types/OcrTypes.hpp"

#include <string>

namespace loupe {

class ReaderFormatter {
public:
    [[nodiscard]] std::string format(const OcrResult& result) const;
    [[nodiscard]] bool prefersOverlay(const OcrResult& result) const noexcept;
};

} // namespace loupe

