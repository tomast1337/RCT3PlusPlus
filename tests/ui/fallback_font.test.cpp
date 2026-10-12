// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/fallback_font.hpp"

#include "ui/text_layout.hpp"

#include <array>
#include <doctest/doctest.h>
#include <vector>

using namespace rct3::ui;

namespace
{
    Glyph square(float size, float advance, TextureId page)
    {
        return { { 0, -size, size, 0 }, { 0, 0, 1, 1 }, page, advance };
    }

    // Primary: 'a' and 'H' only, 10 tall capitals, line height 20, descent 5, page 1.
    Font primary()
    {
        const std::array<CodedGlyph, 2> glyphs = { { { u'a', square(6, 10, 1) }, { u'H', square(10, 10, 1) } } };
        return Font::from_glyphs(glyphs, 20, 5);
    }

    // Fallback: its own 'a', and 'b' and 'c' the primary lacks, on page 2, with different line metrics.
    Font fallback()
    {
        const std::array<CodedGlyph, 3> glyphs = { { { u'a', square(4, 7, 2) }, { u'b', square(8, 12, 2) }, { u'c', square(8, 12, 2) } } };
        return Font::from_glyphs(glyphs, 30, 2);
    }

    std::vector<std::u16string_view> texts(const std::vector<TextLine>& lines)
    {
        std::vector<std::u16string_view> out;
        for (const TextLine& l : lines)
            out.push_back(l.text);
        return out;
    }
} // namespace

TEST_CASE("the primary font's own glyphs win; the fallback fills the gaps")
{
    const Font f = primary().with_fallback(fallback());
    REQUIRE(f.find(u'a'));
    CHECK(f.find(u'a')->page == 1);
    REQUIRE(f.find(u'b'));
    CHECK(f.find(u'b')->page == 2);
    CHECK_FALSE(f.find(u'z'));
    // Line metrics stay the primary's.
    CHECK(f.line_height() == 20);
    CHECK(f.ascent() == 15);
    // The fallback is a value: merging leaves both inputs as they were.
    CHECK_FALSE(primary().find(u'b'));
}

TEST_CASE("first glyph with a code wins when building from glyphs")
{
    const std::array<CodedGlyph, 2> glyphs = { { { u'x', square(1, 3, 0) }, { u'x', square(1, 9, 0) } } };
    const Font f = Font::from_glyphs(glyphs, 10, 2);
    REQUIRE(f.find(u'x'));
    CHECK(f.find(u'x')->advance == 3);
}

TEST_CASE("borrowed glyphs sit on the primary baseline")
{
    const Font f = primary().with_fallback(fallback());
    DrawList list;
    draw_text(list, f, u"ab", { 0, 0, 100, 100 }, { });
    REQUIRE(list.vertices().size() == 8);
    // Baseline at the primary ascent, 15: 'a' is 6 tall, 'b' 8 tall, both end on it.
    CHECK(list.vertices()[0].y == 9);
    CHECK(list.vertices()[2].y == 15);
    CHECK(list.vertices()[4].y == 7);
    CHECK(list.vertices()[6].y == 15);
    CHECK(list.vertices()[4].x == 10);
    REQUIRE(list.batches().size() == 2);
    CHECK(list.batches()[1].texture == 2);
}

TEST_CASE("width and line breaks count borrowed glyphs")
{
    const Font f = primary().with_fallback(fallback());
    CHECK(text_width(f, u"abc") == 34);
    using V = std::vector<std::u16string_view>;
    CHECK(texts(break_lines(f, u"abcab", 30)) == V { u"ab", u"ca", u"b" });
}

TEST_CASE("the fallback is sized by capital height")
{
    CHECK(cap_height(primary()) == 10);
    // No 'H': the ascent stands in.
    CHECK(cap_height(fallback()) == 28);
    // Capitals 10 tall from a font whose capitals are 0.625 em: an em of 16.
    CHECK(fallback_em_pixels(10, 0.625f) == 16);
    CHECK(fallback_em_pixels(9, 0.625f) == 14);
    CHECK(fallback_em_pixels(0, 0.625f) == 1);
    CHECK(fallback_em_pixels(9, 0) == 1);
    SUBCASE("pixel fonts move to their grid when close")
    {
        CHECK(fallback_em_pixels(9, 0.625f, kUnifontGrid) == 16);  // 14.4, within 15% of 16
        CHECK(fallback_em_pixels(7, 0.625f, kUnifontGrid) == 11);  // 11.2, too far from 16
        CHECK(fallback_em_pixels(19, 0.625f, kUnifontGrid) == 32); // 30.4, within 15% of 32
    }
}
