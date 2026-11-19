// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/font.hpp"

#include "core/maybe.hpp"
#include "core/types.hpp"
#include "ovl/reference_names.hpp"

#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kFontTag = "fct";
        constexpr std::string_view kTextureTag = "tex";
        constexpr usize kItemSize = 112; // twenty-eight 32-bit words
        constexpr usize kFaceOffset = 40;
        constexpr usize kFaceSize = 64;
        constexpr usize kReferenceOffset = 20;
        constexpr u32 kMaxGlyphs = 1u << 20;

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        u16 u16_at(std::span<const std::byte> b, usize at)
        {
            u16 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        f32 f32_at(std::span<const std::byte> b, usize at)
        {
            f32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::optional<Font> read_font(const AddressSpace& space, const ReferenceNames& refs, u32 address)
        {
            const auto raw = space.bytes_at(address, kItemSize);
            if (!raw)
                return std::nullopt;

            Font font;
            for (usize i = 0; i < 3; ++i)
                font.metrics[i] = f32_at(*raw, i * 4);
            font.first_char = u32_at(*raw, 12);
            // The file stores the end of the character range: for fonts with a code table `first_char` is 0 and
            // this is the glyph count.
            const u32 end_char = u32_at(*raw, 16);
            font.texture = refs.find(address + kReferenceOffset, kTextureTag);
            font.flag = u32_at(*raw, 28);
            font.weight = u32_at(*raw, 36);

            // The face name is a fixed buffer. Bytes after the terminator are leftovers and not part of the name.
            const auto buffer = raw->subspan(kFaceOffset, kFaceSize);
            const void* end = std::memchr(buffer.data(), 0, buffer.size());
            if (!end)
                return std::nullopt;
            font.face = std::string_view(reinterpret_cast<const char*>(buffer.data()), static_cast<usize>(static_cast<const std::byte*>(end) - buffer.data()));

            const u32 glyph_address = u32_at(*raw, 104);
            const u32 code_address = u32_at(*raw, 108);
            if (end_char < font.first_char)
                return std::nullopt;
            const u32 glyph_count = end_char - font.first_char;
            if (glyph_count > kMaxGlyphs)
                return std::nullopt;

            const auto rows = space.bytes_at(glyph_address, usize { glyph_count } * kFontGlyphSize);
            if (!rows)
                return std::nullopt;
            std::optional<std::span<const std::byte>> codes;
            if (code_address != 0)
            {
                codes = space.bytes_at(code_address, usize { glyph_count } * 2);
                if (!codes)
                    return std::nullopt;
            }

            font.has_code_table = codes.has_value();
            font.glyphs.reserve(glyph_count);
            for (u32 i = 0; i < glyph_count; ++i)
            {
                const auto row = rows->subspan(usize { i } * kFontGlyphSize, kFontGlyphSize);
                FontGlyph g;
                g.code = codes ? u16_at(*codes, usize { i } * 2) : font.first_char + i;
                g.advance = f32_at(row, 0);
                g.lead = f32_at(row, 4);
                // Word 2 of the row holds leftover bytes, not data.
                for (usize k = 0; k < 4; ++k)
                    g.texcoords[k] = f32_at(row, 12 + k * 4);
                g.shift[0] = f32_at(row, 28);
                g.shift[1] = f32_at(row, 32);
                g.page = u16_at(row, 10);
                font.glyphs.push_back(g);
            }
            return font;
        }

    } // namespace

    std::optional<const FontGlyph&> find_glyph(const Font& font, u32 code) noexcept
    {
        if (font.has_code_table)
        {
            if (code == 0 || code > 0xFFFF)
                return std::nullopt;
            for (const FontGlyph& glyph : font.glyphs)
            {
                if (glyph.code == 0)
                    return std::nullopt;
                if (glyph.code == code)
                    return glyph;
            }
            return std::nullopt;
        }
        if (code < font.first_char)
            return std::nullopt;
        const u64 index = u64 { code } - font.first_char;
        return index < font.glyphs.size() ? core::maybe(&font.glyphs[static_cast<usize>(index)]) : std::nullopt;
    }

    std::vector<FontItem> read_fonts(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceNames refs(part, space);

        std::vector<FontItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kFontTag)
                continue;
            items.push_back({ parts.name, read_font(space, refs, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
