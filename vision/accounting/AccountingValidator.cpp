#include "vision/accounting/AccountingValidator.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_map>

namespace loupe {

std::string AccountingValidator::trim(std::string_view text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

AccountingAssessment AccountingValidator::assess(std::string_view raw, float confidence) const {
    AccountingAssessment result;
    result.normalized = trim(raw);
    if (result.normalized.empty()) return result;

    auto grammarInput = result.normalized;
    for (const auto* symbol : {"€", "£", "¥"}) {
        const std::string currency(symbol);
        if (grammarInput.rfind(currency, 0) == 0) {
            grammarInput.erase(0, currency.size());
            grammarInput = trim(grammarInput);
            break;
        }
    }
    static const std::regex accountingPattern(
        R"(^\s*(?:\$\s*)?(?:\(|-)?(?:\d{1,3}(?:[ ,.]\d{3})+|\d+)(?:[.,]\d+)?%?(?:\))?(?:\s*(?:CR|DR))?\s*$)",
        std::regex::icase);
    result.grammarValid = std::regex_match(grammarInput, accountingPattern);

    const auto hasDigit = std::any_of(result.normalized.begin(), result.normalized.end(),
                                      [](unsigned char c) { return std::isdigit(c) != 0; });
    const auto allowed = std::all_of(result.normalized.begin(), result.normalized.end(),
                                     [](unsigned char c) {
        return std::isdigit(c) || std::isspace(c) || c == '$' || c == ',' || c == '.' ||
               c == '-' || c == '(' || c == ')' || c == '%' || c == 'C' || c == 'R' ||
               c == 'D' || c == 'c' || c == 'r' || c == 'd';
    });
    result.numericLike = hasDigit && (result.grammarValid || allowed);

    if (result.numericLike && confidence < 0.88F) {
        static const std::unordered_map<char, char> confusions{
            {'3', '8'}, {'8', '3'}, {'5', '6'}, {'6', '5'}, {'7', '1'}, {'1', '7'}, {'0', '8'}
        };
        for (size_t i = 0; i < result.normalized.size() && result.alternatives.size() < 3; ++i) {
            const auto match = confusions.find(result.normalized[i]);
            if (match == confusions.end()) continue;
            auto alternate = result.normalized;
            alternate[i] = match->second;
            auto alternateGrammar = alternate;
            for (const auto* symbol : {"€", "£", "¥"}) {
                const std::string currency(symbol);
                if (alternateGrammar.rfind(currency, 0) == 0) {
                    alternateGrammar = trim(alternateGrammar.substr(currency.size()));
                    break;
                }
            }
            if (std::regex_match(alternateGrammar, accountingPattern)) {
                result.alternatives.push_back({std::move(alternate), 0.15F, i});
            }
        }
    }
    return result;
}

void AccountingValidator::annotate(OcrResult& result) const {
    for (auto& token : result.tokens) {
        token.numericLike = assess(token.text, token.confidence).numericLike;
    }
}

} // namespace loupe
