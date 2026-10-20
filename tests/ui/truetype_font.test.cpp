// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/truetype_font.hpp"

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <string_view>
#include <vector>

using namespace rct3::ui;

namespace
{

    using Bytes = std::vector<std::uint8_t>;

    constexpr std::uint32_t kVersion1 = 1U << 16;         // 1.0 in the format's 16.16 fixed point
    constexpr std::uint32_t kMaxpVersionShort = 5U << 12; // Version 0.5 (stored as 0x5000): glyph count only

    void u16(Bytes& b, int v)
    {
        b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    }

    void u32(Bytes& b, std::uint32_t v)
    {
        u16(b, static_cast<int>(v >> 16));
        u16(b, static_cast<int>(v & 0xFFFF));
    }

    // A made-up font with an em of 1000 units, ascender 800, descender 200 and three glyphs: 0 (missing, empty),
    // 1 (a 500 x 700 box, advance 600) and 2 (empty, advance 300). Codes 'A' and 'H' map to glyph 1, 'B' to 2.
    Bytes tiny_font()
    {
        Bytes head(54, 0);
        head[18] = 1000 >> 8;
        head[19] = 1000 & 0xFF; // Units per em; index-to-location format 0 (short)

        Bytes hhea;
        u32(hhea, kVersion1);
        u16(hhea, 800);  // Ascender
        u16(hhea, -200); // Descender
        hhea.resize(34, 0);
        u16(hhea, 3); // Advance widths given

        Bytes maxp;
        u32(maxp, kMaxpVersionShort);
        u16(maxp, 3);

        Bytes hmtx;
        for (const int advance : { 500, 600, 300 })
        {
            u16(hmtx, advance);
            u16(hmtx, 0);
        }

        Bytes glyf;
        u16(glyf, 1); // One contour
        for (const int v : { 0, 0, 500, 700 })
            u16(glyf, v);
        u16(glyf, 3); // Last point of the contour
        u16(glyf, 0); // No instructions
        for (int i = 0; i < 4; ++i)
            glyf.push_back(1); // On the curve, coordinates as 16-bit deltas
        for (const int dx : { 0, 0, 500, 0 })
            u16(glyf, dx);
        for (const int dy : { 0, 700, 0, -700 })
            u16(glyf, dy);

        Bytes loca;
        for (const auto offset : { std::size_t { 0 }, std::size_t { 0 }, glyf.size(), glyf.size() })
            u16(loca, static_cast<int>(offset / 2));

        // Format 4: 'A'..'B' to glyphs 1..2, 'H' to glyph 1, and the closing segment.
        Bytes cmap;
        u16(cmap, 0);
        u16(cmap, 1);
        u16(cmap, 3);
        u16(cmap, 1);
        u32(cmap, 12);
        const std::array<int, 3> starts = { 'A', 'H', 0xFFFF };
        const std::array<int, 3> ends = { 'B', 'H', 0xFFFF };
        const std::array<int, 3> deltas = { 1 - 'A', 1 - 'H', 1 };
        u16(cmap, 4);
        u16(cmap, 16 + 8 * 3); // Length
        u16(cmap, 0);
        u16(cmap, 6); // Segments times 2
        u16(cmap, 4); // Search range
        u16(cmap, 1); // Entry selector
        u16(cmap, 2); // Range shift
        for (const int e : ends)
            u16(cmap, e);
        u16(cmap, 0);
        for (const int s : starts)
            u16(cmap, s);
        for (const int d : deltas)
            u16(cmap, d);
        for (int i = 0; i < 3; ++i)
            u16(cmap, 0);

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
        u32(file, kVersion1);
        u16(file, static_cast<int>(tables.size()));
        u16(file, 64);
        u16(file, 2);
        u16(file, 48);
        std::size_t offset = 12 + 16 * tables.size();
        for (const auto& [tag, data] : tables)
        {
            file.insert(file.end(), tag.begin(), tag.end());
            u32(file, 0);
            u32(file, static_cast<std::uint32_t>(offset));
            u32(file, static_cast<std::uint32_t>(data->size()));
            offset += (data->size() + 3) & ~std::size_t { 3 };
        }
        for (const auto& [_, data] : tables)
        {
            file.insert(file.end(), data->begin(), data->end());
            file.resize((file.size() + 3) & ~std::size_t { 3 }, 0);
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
    const auto x = static_cast<std::uint32_t>(a->uv.x0 * 32) + 2;
    const auto y = static_cast<std::uint32_t>(a->uv.y0 * 32) + 3;
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
