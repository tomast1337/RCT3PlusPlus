// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/truetype_font.hpp"

#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <string_view>
#include <vector>

using namespace rct3::ui;
using namespace rct3::ui::font;

namespace
{

    using Bytes = std::vector<u8>;

    constexpr u32 kVersion1 = 1U << 16;         // 1.0 in the format's 16.16 fixed point
    constexpr u32 kMaxpVersionShort = 5U << 12; // Version 0.5 (stored as 0x5000): glyph count only

    void put_u16(Bytes& b, i32 v)
    {
        b.push_back(static_cast<u8>((v >> 8) & 0xFF));
        b.push_back(static_cast<u8>(v & 0xFF));
    }

    void put_u32(Bytes& b, u32 v)
    {
        put_u16(b, static_cast<i32>(v >> 16));
        put_u16(b, static_cast<i32>(v & 0xFFFF));
    }

    // A made-up font with an em of 1000 units, ascender 800, descender 200 and three glyphs: 0 (missing, empty),
    // 1 (a 500 x 700 box, advance 600) and 2 (empty, advance 300). Codes 'A' and 'H' map to glyph 1, 'B' to 2.
    Bytes tiny_font()
    {
        Bytes head(54, 0);
        head[18] = 1000 >> 8;
        head[19] = 1000 & 0xFF; // Units per em; index-to-location format 0 (short)

        Bytes hhea;
        put_u32(hhea, kVersion1);
        put_u16(hhea, 800);  // Ascender
        put_u16(hhea, -200); // Descender
        hhea.resize(34, 0);
        put_u16(hhea, 3); // Advance widths given

        Bytes maxp;
        put_u32(maxp, kMaxpVersionShort);
        put_u16(maxp, 3);

        Bytes hmtx;
        for (const i32 advance : { 500, 600, 300 })
        {
            put_u16(hmtx, advance);
            put_u16(hmtx, 0);
        }

        Bytes glyf;
        put_u16(glyf, 1); // One contour
        for (const i32 v : { 0, 0, 500, 700 })
            put_u16(glyf, v);
        put_u16(glyf, 3); // Last point of the contour
        put_u16(glyf, 0); // No instructions
        for (i32 i = 0; i < 4; ++i)
            glyf.push_back(1); // On the curve, coordinates as 16-bit deltas
        for (const i32 dx : { 0, 0, 500, 0 })
            put_u16(glyf, dx);
        for (const i32 dy : { 0, 700, 0, -700 })
            put_u16(glyf, dy);

        Bytes loca;
        for (const auto offset : { usize { 0 }, usize { 0 }, glyf.size(), glyf.size() })
            put_u16(loca, static_cast<i32>(offset / 2));

        // Format 4: 'A'..'B' to glyphs 1..2, 'H' to glyph 1, and the closing segment.
        Bytes cmap;
        put_u16(cmap, 0);
        put_u16(cmap, 1);
        put_u16(cmap, 3);
        put_u16(cmap, 1);
        put_u32(cmap, 12);
        const std::array<i32, 3> starts = { 'A', 'H', 0xFFFF };
        const std::array<i32, 3> ends = { 'B', 'H', 0xFFFF };
        const std::array<i32, 3> deltas = { 1 - 'A', 1 - 'H', 1 };
        put_u16(cmap, 4);
        put_u16(cmap, 16 + 8 * 3); // Length
        put_u16(cmap, 0);
        put_u16(cmap, 6); // Segments times 2
        put_u16(cmap, 4); // Search range
        put_u16(cmap, 1); // Entry selector
        put_u16(cmap, 2); // Range shift
        for (const i32 e : ends)
            put_u16(cmap, e);
        put_u16(cmap, 0);
        for (const i32 s : starts)
            put_u16(cmap, s);
        for (const i32 d : deltas)
            put_u16(cmap, d);
        for (i32 i = 0; i < 3; ++i)
            put_u16(cmap, 0);

        const std::array<std::pair<std::string_view, const Bytes*>, 7> tables = { {
            { "cmap", &cmap },
            { "glyf", &glyf },
            { "head", &head },
            { "hhea", &hhea },
            { "hmtx", &hmtx },
            { "loca", &loca },
            { "maxp", &maxp },
        } };
        Bytes file;
        put_u32(file, kVersion1);
        put_u16(file, static_cast<i32>(tables.size()));
        put_u16(file, 64);
        put_u16(file, 2);
        put_u16(file, 48);
        usize offset = 12 + 16 * tables.size();
        for (const auto& [tag, data] : tables)
        {
            file.insert(file.end(), tag.begin(), tag.end());
            put_u32(file, 0);
            put_u32(file, static_cast<u32>(offset));
            put_u32(file, static_cast<u32>(data->size()));
            offset += (data->size() + 3) & ~usize { 3 };
        }
        for (const auto& [_, data] : tables)
        {
            file.insert(file.end(), data->begin(), data->end());
            file.resize((file.size() + 3) & ~usize { 3 }, 0);
        }
        return file;
    }

    const BakedGlyph* find(const BakedFont& font, char16_t code)
    {
        for (const BakedGlyph& g : font.glyphs)
        {
            if (g.code == code)
                return &g;
        }
        return nullptr;
    }

} // namespace

TEST_CASE("line metrics come from the font's tables")
{
    const auto file = read_truetype(tiny_font());
    REQUIRE(file);
    CHECK(file->ascent_per_em == doctest::Approx(0.8f));
    CHECK(file->descent_per_em == doctest::Approx(0.2f));
    CHECK(file->cap_per_em == doctest::Approx(0.7f));
}

TEST_CASE("files that are not TrueType are refused")
{
    CHECK_FALSE(read_truetype({ }).has_value());
    CHECK_FALSE(read_truetype(Bytes(64, 0)).has_value());
    CHECK_FALSE(load_truetype("does-not-exist.ttf").has_value());
}

TEST_CASE("baking renders the asked codes the font has")
{
    const auto file = read_truetype(tiny_font());
    REQUIRE(file);
    const std::array<CodeRange, 1> ranges = { { { u'A', u'C' } } };
    const auto baked = bake_truetype(*file, 10, ranges, { .page_side = 32, .hard_edges = true });
    REQUIRE(baked);
    CHECK(baked->line_height == 10);
    CHECK(baked->descent == 2);
    REQUIRE(baked->glyphs.size() == 2); // 'C' is not in the font
    REQUIRE(baked->pages.size() == 1);

    const BakedGlyph* a = find(*baked, u'A');
    REQUIRE(a);
    CHECK(a->advance == 6);
    CHECK(a->box == Rect { 0, -7, 5, 0 });
    CHECK(a->uv.width() == doctest::Approx(5.0f / 32));
    CHECK(a->uv.height() == doctest::Approx(7.0f / 32));

    // The middle of the box is covered.
    const auto& page = baked->pages[0];
    const auto x = static_cast<u32>(a->uv.x0 * 32) + 2;
    const auto y = static_cast<u32>(a->uv.y0 * 32) + 3;
    CHECK(page.pixels[((y * 32) + x) * 4 + 3] == 255);

    const BakedGlyph* b = find(*baked, u'B');
    REQUIRE(b);
    CHECK(b->advance == 3);
    CHECK(b->box.empty());

    SUBCASE("as a font on uploaded pages")
    {
        const std::array<Font::Page, 1> pages = { { { 7, 32, 32 } } };
        const Font f = font_from_baked(*baked, pages);
        REQUIRE(f.find(u'A'));
        CHECK(f.find(u'A')->page == 7);
        CHECK(f.line_height() == 10);
        CHECK(f.ascent() == 8);
        CHECK_FALSE(f.find(u'C'));
    }
}

TEST_CASE("bad sizes are refused")
{
    const auto file = read_truetype(tiny_font());
    REQUIRE(file);
    const std::array<CodeRange, 1> ranges = { { { u'A', u'A' } } };
    CHECK_FALSE(bake_truetype(*file, 0, ranges).has_value());
    CHECK_FALSE(bake_truetype(*file, 10, ranges, { .page_side = 0 }).has_value());
}
