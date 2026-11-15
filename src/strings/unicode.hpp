// SPDX-License-Identifier: GPL-3.0-or-later
//
// Converting between the encodings the program meets: UTF-16 inside (the game's strings and the interface)
// and UTF-8 outside (files, the system clipboard, logs). Bad input never fails: an unpaired surrogate or a
// broken UTF-8 sequence becomes U+FFFD.
#pragma once

#include <string>
#include <string_view>

namespace rct3::strings
{

    constexpr char32_t kReplacement = 0xFFFD;

    [[nodiscard]] constexpr bool is_high_surrogate(char32_t c) noexcept
    {
        return c >= 0xD800 && c <= 0xDBFF;
    }

    [[nodiscard]] constexpr bool is_low_surrogate(char32_t c) noexcept
    {
        return c >= 0xDC00 && c <= 0xDFFF;
    }

    /// The character past U+FFFF that the surrogate pair `high`, `low` stands for.
    [[nodiscard]] constexpr char32_t from_surrogates(char32_t high, char32_t low) noexcept
    {
        return 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
    }

    /// Appends `c` as UTF-8: one byte for ASCII, up to four past U+FFFF.
    void append_utf8(std::string& out, char32_t c);

    /// Appends `c` as one code unit, or two for characters past U+FFFF.
    void append_utf16(std::u16string& out, char32_t c);

    /// `text` for searching: lower case, and the accented Latin letters (U+00C0 to U+00FF) as their plain
    /// ones, so "Cafe" finds "Café".
    [[nodiscard]] std::u16string search_folded(std::u16string_view text);

    [[nodiscard]] std::string to_utf8(std::u16string_view text);
    [[nodiscard]] std::u16string to_utf16(std::string_view utf8);

} // namespace rct3::strings
