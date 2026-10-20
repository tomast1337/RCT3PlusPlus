// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/font.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    // A font with cell size 10: line height 9, descent 2.
    ovl::Font font_with(std::vector<ovl::FontGlyph> glyphs, bool code_table = true)
    {
        ovl::Font f;
        f.metrics = { 0.1f, 0.9f, 0.2f };
        f.has_code_table = code_table;
        f.glyphs = std::move(glyphs);
        return f;
    }

    ovl::FontGlyph glyph(std::uint32_t code, float advance, std::array<float, 4> uv = { }, std::array<float, 2> shift = { }, std::uint16_t page = 0)
    {
        ovl::FontGlyph g;
        g.code = code;
        g.advance = advance;
        g.texcoords = uv;
        g.shift = shift;
        g.page = page;
        return g;
    }

    const std::array<Font::Page, 2> kPages = { { { 5, 100, 100 }, { 6, 100, 100 } } };
} // namespace

TEST_CASE("metrics scale with the cell")
{
    const Font f = Font::from(font_with({ }), kPages);
    CHECK(f.cell() == doctest::Approx(10));
    // A line is the line height and the descent: 9 + 2, the baseline 9 down.
    CHECK(f.line_height() == doctest::Approx(11));
    CHECK(f.ascent() == doctest::Approx(9));
}

TEST_CASE("fonts of the roomy face leave room for the descent twice")
{
    const Font f = Font::from(font_with({ }), kPages, kRoomyFace);
    CHECK(f.line_height() == doctest::Approx(13));
    CHECK(f.ascent() == doctest::Approx(11));
}

TEST_CASE("glyph boxes sit on the baseline")
{
    // 8 x 10 texels, one texel right of the pen, bottom 2 below the baseline.
    const Font f = Font::from(font_with({ glyph('g', 0.6f, { 0.1f, 0.5f, 0.18f, 0.6f }, { 1, -2 }, 1) }), kPages);
    auto g = f.find(u'g');
    REQUIRE(g);
    CHECK(g->advance == doctest::Approx(6));
    CHECK(g->page == 6);
    CHECK(g->box.x0 == doctest::Approx(1));
    CHECK(g->box.x1 == doctest::Approx(9));
    CHECK(g->box.y1 == doctest::Approx(2));
    CHECK(g->box.y0 == doctest::Approx(-8));
    // The top of the glyph is the larger v.
    CHECK(g->uv.y0 == doctest::Approx(0.6f));
    CHECK(g->uv.y1 == doctest::Approx(0.5f));
}

TEST_CASE("glyph lookup follows the file's rules")
{
    SUBCASE("first match wins and the terminator ends the table")
    {
        const Font f = Font::from(font_with({ glyph('a', 0.5f), glyph('a', 0.9f), glyph(0, 0), glyph('b', 0.5f) }), kPages);
        REQUIRE(f.find(u'a'));
        CHECK(f.find(u'a')->advance == doctest::Approx(5));
        CHECK_FALSE(f.find(u'b'));
        CHECK_FALSE(f.find(u'\0'));
    }
    SUBCASE("range fonts count from the first code")
    {
        ovl::Font range = font_with({ glyph(0, 0.1f), glyph(0, 0.2f) }, false);
        range.first_char = '0';
        const Font f = Font::from(range, kPages);
        REQUIRE(f.find(u'1'));
        CHECK(f.find(u'1')->advance == doctest::Approx(2));
        CHECK_FALSE(f.find(u'2'));
    }
    SUBCASE("blank glyphs and glyphs on missing pages advance but draw nothing")
    {
        const Font f = Font::from(font_with({ glyph(' ', 0.3f, { 0, 1, 0, 1 }), glyph('x', 0.5f, { 0, 0, 0.1f, 0.1f }, { }, 7) }), kPages);
        REQUIRE(f.find(u' '));
        CHECK(f.find(u' ')->box.empty());
        REQUIRE(f.find(u'x'));
        CHECK(f.find(u'x')->box.empty());
        CHECK(f.find(u'x')->advance == doctest::Approx(5));
    }
    SUBCASE("a font without a cell size is empty")
    {
        ovl::Font bad = font_with({ glyph('a', 1) });
        bad.metrics[0] = 0;
        CHECK_FALSE(Font::from(bad, kPages).find(u'a'));
    }
}
