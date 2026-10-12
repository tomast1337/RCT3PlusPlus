// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/text_layout.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    // Every letter and the hyphen advance 10 and draw a 10 x 10 box on the baseline; space advances 5.
    // Line height 20, descent 5.
    Font test_font()
    {
        ovl::Font f;
        f.metrics = { 0.1f, 2.0f, 0.5f };
        f.has_code_table = true;
        for (char16_t c = u'a'; c <= u'z'; ++c)
            f.glyphs.push_back({ c, 1, 0, { 0, 0, 0.1f, 0.1f }, { 0, 0 }, 0 });
        f.glyphs.push_back({ u'-', 1, 0, { 0, 0, 0.1f, 0.1f }, { 0, 0 }, 0 });
        f.glyphs.push_back({ u' ', 0.5f, 0, { 0, 1, 0, 1 }, { 0, 0 }, 0 });
        const std::array<Font::Page, 1> pages = { { { 3, 100, 100 } } };
        return Font::from(f, pages);
    }

    std::vector<std::u16string_view> texts(const std::vector<TextLine>& lines)
    {
        std::vector<std::u16string_view> out;
        for (const TextLine& l : lines)
            out.push_back(l.text);
        return out;
    }
} // namespace

TEST_CASE("width is the sum of advances; unknown units add nothing")
{
    const Font f = test_font();
    CHECK(text_width(f, u"ab c") == doctest::Approx(35));
    CHECK(text_width(f, u"A") == 0);
}

TEST_CASE("lines break at feeds, spaces and after hyphens")
{
    const Font f = test_font();
    using V = std::vector<std::u16string_view>;
    CHECK(texts(break_lines(f, u"ab\ncd", 0)) == V { u"ab", u"cd" });
    CHECK(texts(break_lines(f, u"ab cd ef", 0)) == V { u"ab cd ef" });
    // 55 fits "ab cd" (45) but not "ab cd ef" (70).
    CHECK(texts(break_lines(f, u"ab cd ef", 55)) == V { u"ab cd", u"ef" });
    CHECK(texts(break_lines(f, u"abc-def", 45)) == V { u"abc-", u"def" });
    SUBCASE("a word wider than the box is cut")
    {
        CHECK(texts(break_lines(f, u"abcdef", 30)) == V { u"abc", u"def" });
    }
    SUBCASE("widths leave out the space at the break")
    {
        const auto lines = break_lines(f, u"ab   cd", 30);
        REQUIRE(lines.size() == 2);
        CHECK(lines[0].width == doctest::Approx(20));
        CHECK(lines[1].text == u"cd");
    }
    SUBCASE("empty text is one empty line")
    {
        CHECK(texts(break_lines(f, u"", 10)) == V { u"" });
    }
}

TEST_CASE("draw_text aligns lines in the box")
{
    const Font f = test_font();
    DrawList list;
    SUBCASE("start: top-left, baseline one ascent down")
    {
        draw_text(list, f, u"ab", { 100, 50, 200, 150 }, { });
        REQUIRE(list.vertices().size() == 8);
        CHECK(list.vertices()[0].x == 100);
        CHECK(list.vertices()[0].y == 55); // Baseline 65, box 10 tall
        CHECK(list.vertices()[4].x == 110);
        CHECK(list.batches()[0].texture == 3);
    }
    SUBCASE("centre and end")
    {
        draw_text(list, f, u"ab", { 100, 50, 200, 150 }, { { 0, 0, 0, 255 }, Align::Centre, Align::End, false });
        CHECK(list.vertices()[0].x == 140);
        CHECK(list.vertices()[0].y == 135); // Line top 130
    }
    SUBCASE("spaces move the pen without drawing")
    {
        draw_text(list, f, u"a b", { 0, 0, 100, 100 }, { });
        REQUIRE(list.vertices().size() == 8);
        CHECK(list.vertices()[4].x == 15);
    }
    SUBCASE("wrapped lines go down one line height each")
    {
        draw_text(list, f, u"ab cd", { 0, 0, 30, 100 }, { { 0, 0, 0, 255 }, Align::Start, Align::Start, true });
        REQUIRE(list.vertices().size() == 16);
        CHECK(list.vertices()[8].y == list.vertices()[0].y + 20);
    }
}
