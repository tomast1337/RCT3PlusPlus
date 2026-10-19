// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/loading_screen.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3::ui;
using namespace rct3::ui::loading_screen;

namespace
{
    Glyph square(float size, float advance)
    {
        return { { 0, -size, size, 0 }, { 0, 0, 1, 1 }, 9, advance };
    }

    bool inside(const Rect& inner, const Rect& outer)
    {
        return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 && inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
    }

    std::size_t quads_with(const DrawList& list, TextureId texture)
    {
        std::size_t n = 0;
        for (const Batch& b : list.batches())
        {
            if (b.kind == BatchKind::Quads && b.texture == texture)
                n += b.index_count / 6;
        }
        return n;
    }

    std::size_t shapes(const DrawList& list)
    {
        std::size_t n = 0;
        for (const Batch& b : list.batches())
        {
            if (b.kind == BatchKind::Shapes)
                n += b.index_count / 6;
        }
        return n;
    }

    const Parts kAll { .logo_width = 512, .logo_height = 256, .heading = true, .bar = true, .stages = 4, .detail_lines = 2 };
} // namespace

TEST_CASE("the column stands in the middle, on screens of every shape, its parts in order")
{
    for (const Rect screen : { Rect { 0, 0, 1280, 720 }, Rect { 0, 0, 640, 480 }, Rect { 0, 0, 3840, 2160 }, Rect { 0, 0, 480, 900 } })
    {
        CAPTURE(screen.x1);
        const Layout l = layout_for(screen, kAll);
        for (const Rect& part : { l.logo, l.heading, l.bar, l.stages, l.detail })
        {
            CHECK_FALSE(part.empty());
            CHECK(inside(part, screen));
        }
        CHECK(l.logo.y1 <= l.heading.y0);
        CHECK(l.heading.y1 <= l.bar.y0);
        CHECK(l.bar.y1 <= l.stages.y0);
        CHECK(l.stages.y1 <= l.detail.y0);
        CHECK(l.stages.height() == doctest::Approx(4 * l.row));
        // The logo keeps its shape, centred.
        CHECK(l.logo.width() / l.logo.height() == doctest::Approx(2).epsilon(0.02));
        CHECK((l.logo.x0 + l.logo.x1) / 2 == doctest::Approx(screen.width() / 2).epsilon(0.01));
        // The block is centred up and down.
        CHECK(l.logo.y0 - screen.y0 == doctest::Approx(screen.y1 - l.detail.y1).epsilon(0.02));
    }
}

TEST_CASE("the backdrop splits through the logo's middle, a band behind it, the line just above")
{
    const Rect screen { 0, 0, 1280, 720 };
    const Layout l = layout_for(screen, kAll);
    const float middle = (l.logo.y0 + l.logo.y1) / 2;
    CHECK(l.dark.y1 == doctest::Approx(middle).epsilon(0.01));
    CHECK(l.light.y0 == l.dark.y1);
    CHECK(l.dark.y0 == 0);
    CHECK(l.light.y1 == 720);
    CHECK(l.band.y0 == l.logo.y0);
    CHECK(l.band.y1 == l.logo.y1);
    CHECK(l.band.width() == 1280);
    CHECK(l.line.y1 < l.band.y0);
    CHECK(l.line.width() == 1280);
    // Without a logo, through the screen's middle.
    CHECK(layout_for(screen, { }).dark.y1 == 360);
    CHECK(layout_for(screen, { }).band.empty());
}

TEST_CASE("parts left out take no room")
{
    const Rect screen { 0, 0, 1280, 720 };
    const Layout l = layout_for(screen, { .logo_width = 512, .logo_height = 256 });
    CHECK(l.heading.empty());
    CHECK(l.bar.empty());
    CHECK(l.stages.empty());
    CHECK(l.detail.empty());
    CHECK((l.logo.y0 + l.logo.y1) / 2 == doctest::Approx(360).epsilon(0.01));
    CHECK(layout_for(screen, { }).logo.empty());
}

TEST_CASE("the bar fills from the left, its ends round however little is done")
{
    const Rect bar { 100, 50, 500, 66 };
    CHECK(bar_fill(bar, 0).empty());
    CHECK(bar_fill(bar, 0.5f).x1 == doctest::Approx(300));
    CHECK(bar_fill(bar, 0.01f).width() == doctest::Approx(16)); // As wide as it is tall
    CHECK(bar_fill(bar, 2).x1 == doctest::Approx(500));
    CHECK(bar_fill(bar, 0.5f).y0 == bar.y0);
}

TEST_CASE("progress counts the stages done and reads as a percentage")
{
    CHECK(stage_progress(0, 4) == 0);
    CHECK(stage_progress(1, 4) == doctest::Approx(0.25));
    CHECK(stage_progress(7, 4) == 1);
    CHECK(stage_progress(0, 0) == 1);
    CHECK(percent_text(0.25f) == u"25%");
    CHECK(percent_text(1.4f) == u"100%");
    CHECK(percent_text(0.666f) == u"67%");
}

TEST_CASE("the screen draws the logo, its text, the bar and a marker per stage")
{
    const std::array<CodedGlyph, 1> glyphs = { { { u'a', square(6, 7) } } };
    const Font font = Font::from_glyphs(glyphs, 10, 2);
    const Rect screen { 0, 0, 1280, 720 };
    const std::array<Stage, 3> stages = { { { u"a", StageState::Done }, { u"aa", StageState::Current }, { u"aaa", StageState::Waiting } } };

    SUBCASE("everything")
    {
        const Look look { Picture { 5, 512, 256 }, &font, &font, &font };
        DrawList list;
        draw(list, screen, look, { u"aa", stages, u"a", u"aa", 0.5f });
        CHECK(quads_with(list, 5) == 1);
        // Heading 2 ("50%" has no glyph here); stages 1 + 2 + 3; detail 1, warning 2.
        CHECK(quads_with(list, 9) == 11);
        // The track and its fill, and the markers: one each, two for the one under way.
        CHECK(shapes(list) == 2 + 4);
    }
    SUBCASE("before the fonts, only the backdrop and the logo")
    {
        const Look look { Picture { 5, 512, 256 } };
        DrawList list;
        draw(list, screen, look, { });
        CHECK(quads_with(list, 5) == 1);
        CHECK(quads_with(list, 9) == 0);
        CHECK(shapes(list) == 0);
    }
    SUBCASE("nothing done yet: an empty track")
    {
        const Look look { std::nullopt, &font, &font, &font };
        DrawList list;
        draw(list, screen, look, { u"aa", { }, { }, { }, 0.0f });
        CHECK(shapes(list) == 1);
    }
}
