// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/money_text.hpp"

#include <format>

namespace rct3::ui
{

    namespace
    {
        constexpr std::string_view kCurrencyPrefix = "Money"; ///< + the currency's number + one of the letters below
        constexpr char kBefore = 'a';
        constexpr char kAfter = 'b';
        constexpr char kDecimal = 'd';
        constexpr std::int64_t kHundredths = 100;
        constexpr std::size_t kGroup = 3;

        std::u16string part(const StringTable& strings, int index, char letter)
        {
            return std::u16string(strings.get(std::format("{}{}{}", kCurrencyPrefix, index, letter)));
        }

        /// `n` in decimal digits, by threes with `separator` between them when it is not empty.
        std::u16string grouped(std::uint64_t n, std::u16string_view separator)
        {
            const std::string digits = std::to_string(n);
            std::u16string out;
            for (std::size_t i = 0; i < digits.size(); ++i)
            {
                if (i > 0 && (digits.size() - i) % kGroup == 0)
                    out += separator;
                out += static_cast<char16_t>(digits[i]);
            }
            return out;
        }
    } // namespace

    Currency currency(const StringTable& strings, int index)
    {
        return { part(strings, index, kBefore), part(strings, index, kAfter), part(strings, index, kDecimal) };
    }

    std::u16string money_text(const Currency& currency, std::int64_t amount)
    {
        const bool negative = amount < 0;
        // The magnitude without overflow, even for the smallest value.
        const std::uint64_t magnitude = negative ? 0 - static_cast<std::uint64_t>(amount) : static_cast<std::uint64_t>(amount);
        const bool cents = !currency.decimal.empty();
        const std::u16string_view separator = currency.decimal == u"," ? u"" : u",";

        std::u16string out = negative ? u"-" : u"";
        out += currency.before;
        if (cents)
        {
            const auto rest = magnitude % kHundredths;
            out += grouped(magnitude / kHundredths, separator);
            out += currency.decimal;
            out += static_cast<char16_t>(u'0' + rest / 10);
            out += static_cast<char16_t>(u'0' + rest % 10);
        }
        else
        {
            out += grouped(magnitude, separator);
        }
        out += currency.after;
        return out;
    }

} // namespace rct3::ui
