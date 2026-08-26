#pragma once

#include "core/types/OcrTypes.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace loupe {

struct AccountingAlternative {
    std::string text;
    float relativeScore{};
    size_t uncertainOffset{};
};

struct AccountingAssessment {
    bool numericLike{};
    bool grammarValid{};
    std::string normalized;
    std::vector<AccountingAlternative> alternatives;
};

class AccountingValidator {
public:
    [[nodiscard]] AccountingAssessment assess(std::string_view text, float confidence) const;
    void annotate(OcrResult& result) const;

private:
    [[nodiscard]] static std::string trim(std::string_view text);
};

} // namespace loupe

