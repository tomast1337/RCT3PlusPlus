// SPDX-License-Identifier: GPL-3.0-or-later
#include "strings/unicode.hpp"

namespace rct3::strings
{

    namespace
    {

        void append_utf8(std::string& out, char32_t cp)
        {
            if (cp < 0x80)
            {
                out.push_back(static_cast<char>(cp));
            }
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        // The character starting at `text[i]` and its length in bytes. Overlong forms, surrogates and values
        // past U+10FFFF are refused; a bad sequence is one replacement character for its first byte.
        std::pair<char32_t, std::size_t> decode_utf8(std::string_view text, std::size_t i)
        {
            const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };
            const unsigned char lead = byte(i);
            if (lead < 0x80)
                return { lead, 1 };
            std::size_t length = 0;
            char32_t cp = 0;
            char32_t smallest = 0;
            if ((lead & 0xE0) == 0xC0)
            {
                length = 2;
                cp = lead & 0x1F;
                smallest = 0x80;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                length = 3;
                cp = lead & 0x0F;
                smallest = 0x800;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                length = 4;
                cp = lead & 0x07;
                smallest = 0x10000;
            }
            else
            {
                return { kReplacement, 1 };
            }
            if (i + length > text.size())
                return { kReplacement, 1 };
            for (std::size_t k = 1; k < length; ++k)
            {
                if ((byte(i + k) & 0xC0) != 0x80)
                    return { kReplacement, 1 };
                cp = (cp << 6) | (byte(i + k) & 0x3F);
            }
            if (cp < smallest || cp > 0x10FFFF || is_high_surrogate(cp) || is_low_surrogate(cp))
                return { kReplacement, 1 };
            return { cp, length };
        }

    } // namespace

    void append_utf16(std::u16string& out, char32_t c)
    {
        if (c < 0x10000)
        {
            out.push_back(static_cast<char16_t>(c));
            return;
        }
        c -= 0x10000;
        out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
        out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
    }

    std::string to_utf8(std::u16string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const char16_t unit = text[i];
            if (is_high_surrogate(unit) && i + 1 < text.size() && is_low_surrogate(text[i + 1]))
            {
                append_utf8(out, 0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) + (static_cast<char32_t>(text[i + 1]) - 0xDC00));
                ++i;
            }
            else if (is_high_surrogate(unit) || is_low_surrogate(unit))
            {
                append_utf8(out, kReplacement);
            }
            else
            {
                append_utf8(out, unit);
            }
        }
        return out;
    }

    std::u16string to_utf16(std::string_view utf8)
    {
        std::u16string out;
        out.reserve(utf8.size());
        for (std::size_t i = 0; i < utf8.size();)
        {
            const auto [cp, length] = decode_utf8(utf8, i);
            append_utf16(out, cp);
            i += length;
        }
        return out;
    }

} // namespace rct3::strings
