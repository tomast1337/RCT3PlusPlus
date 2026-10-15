// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/loading_screen.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3::ui;
using namespace rct3::ui::loading_screen;

namespace
{
    // A skin covering a whole texture of its own.
    SkinRef skin(float width, float height, TextureId texture)
    {
        SkinRef s;
        s.layout.xs = { 0, width };
        s.layout.ys = { 0, height };
        s.layout.x_count = 2;
        s.layout.y_count = 2;
        s.texture = texture;
        s.texture_width = width;
        s.texture_height = height;
        return s;
    }

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
            if (b.texture == texture)
                n += b.index_count / 6;
        }
        return n;
    }
} // namespace

TEST_CASE("the band keeps its shape and stays on screens of every shape")
{
    for (const Rect screen : { Rect { 0, 0, 1024, 768 }, Rect { 0, 0, 1920, 1080 }, Rect { 0, 0, 3440, 1080 }, Rect { 0, 0, 600, 1200 }, Rect { 0, 0, 320, 200 } })
    {
        CAPTURE(screen.width());
        CAPTURE(screen.height());
        const Layout l = layout_for(screen, 580, 180);
        CHECK(l.band.width() / l.band.height() == doctest::Approx(580.0 / 180.0).epsilon(0.02));
        CHECK(inside(l.band, screen));
        CHECK(l.band.width() <= screen.width() * kWidthShare + 1);
        CHECK(l.band.height() <= screen.height() * kHeightShare + 1);
        // Centred, to the pixel.
        CHECK(std::abs((l.band.x0 + l.band.x1) - screen.width()) <= 1);
        CHECK(std::abs((l.band.y0 + l.band.y1) - screen.height()) <= 1);
    }
}

TEST_CASE("the width share decides on wide screens, the height share on tall ones")
{
    CHECK(layout_for({ 0, 0, 2000, 2000 }, 500, 100).scale == doctest::Approx(2000 * kWidthShare / 500));
    CHECK(layout_for({ 0, 0, 4000, 400 }, 500, 100).scale == doctest::Approx(400 * kHeightShare / 100));
}

TEST_CASE("the backdrop covers the screen, split across the band, the line above the split")
{
    const Rect screen { 0, 0, 1000, 800 };
    const Layout l = layout_for(screen, 200, 100);
    CHECK(l.dark.y0 == 0);
    CHECK(l.dark.y1 == l.light.y0);
    CHECK(l.light.y1 == 800);
    CHECK(l.dark.width() == 1000);
    CHECK(l.light.width() == 1000);
    CHECK(l.light.y0 > l.band.y0);
    CHECK(l.light.y0 < l.band.y1);
    CHECK(l.line.y1 <= l.light.y0);
    CHECK(l.line.height() > 0);
}

TEST_CASE("the text and the bar sit inside the band and scale with it")
{
    const Layout small = layout_for({ 0, 0, 1000, 1000 }, 581, 181);
    const Layout big = layout_for({ 0, 0, 2000, 2000 }, 581, 181);
    for (const Layout* l : { &small, &big })
    {
        CHECK(inside(l->heading, l->band));
        CHECK(inside(l->step, l->band));
        CHECK(inside(l->bar, l->band));
        CHECK(l->heading.y1 <= l->step.y0);
        CHECK(l->step.y1 <= l->bar.y0);
    }
    CHECK(big.bar.width() == doctest::Approx(small.bar.width() * 2));
}

TEST_CASE("a band without a size leaves only the backdrop")
{
    const Layout l = layout_for({ 0, 0, 640, 480 }, 0, 0);
    CHECK(l.band.empty());
    CHECK(l.scale == 0);
}

TEST_CASE("the band follows the language")
{
    CHECK(band_for("English") == kBand);
    CHECK(band_for("German") == kBand);
    CHECK(band_for("Chinese") == kBandTraditional);
    CHECK(band_for("SChinese") == kBandSimplified);
}

TEST_CASE("progress counts the stages done")
{
    CHECK(stage_progress(0, 4) == 0);
    CHECK(stage_progress(1, 4) == doctest::Approx(0.25));
    CHECK(stage_progress(4, 4) == 1);
    CHECK(stage_progress(7, 4) == 1);
    CHECK(stage_progress(0, 0) == 1);
}

TEST_CASE("the step line names the stage and its percentage")
{
    CHECK(step_line(u"Ground", 0.25f) == u"Ground (25%)");
    CHECK(step_line(u"Ground", 0) == u"Ground (0%)");
    CHECK(step_line(u"Ground", 1.4f) == u"Ground (100%)");
    CHECK(step_line(u"", 0.666f) == u" (67%)");
}

TEST_CASE("the screen draws the band, its text and a bar filled as far as the progress")
{
    const SkinRef band = skin(581, 181, 1); // The size the layout is measured in
    const SkinRef left = skin(4, 10, 2);
    const SkinRef right = skin(4, 10, 3);
    const std::array<CodedGlyph, 1> glyphs = { { { u'a', square(6, 7) } } };
    const Font font = Font::from_glyphs(glyphs, 10, 2);
    const Look look { &band, &left, &right, &font, &font };
    const Rect screen { 0, 0, 1000, 800 };

    SUBCASE("half way")
    {
        DrawList list;
        draw(list, screen, look, { u"aa", u"aaa", 0.5f });
        CHECK(quads_with(list, 1) == 1);
        CHECK(quads_with(list, 2) == 1);
        CHECK(quads_with(list, 3) == 1);
        CHECK(quads_with(list, 9) == 5);
        // The fill reaches half way along the track between the ends.
        const Layout l = layout_for(screen, 581, 181);
        const float track0 = l.bar.x0 + 4 * l.scale;
        const float track1 = l.bar.x1 - 4 * l.scale;
        bool found = false;
        for (const Vertex& v : list.vertices())
        {
            if (v.color[0] == kBarFill.r && v.color[1] == kBarFill.g && v.color[2] == kBarFill.b && v.x > track0 + 1)
            {
                CHECK(v.x == doctest::Approx((track0 + track1) / 2).epsilon(0.01));
                found = true;
            }
        }
        CHECK(found);
    }
    SUBCASE("without progress there is no bar")
    {
        DrawList list;
        draw(list, screen, look, { u"aa", u"", std::nullopt });
        CHECK(quads_with(list, 2) == 0);
        CHECK(quads_with(list, 3) == 0);
        CHECK(quads_with(list, 9) == 2);
    }
    SUBCASE("without a band only the backdrop shows")
    {
        DrawList list;
        draw(list, screen, { }, { u"aa", u"aaa", 0.5f });
        CHECK(list.batches().size() == 1);
        CHECK(list.batches()[0].texture == 0);
        CHECK(list.batches()[0].index_count == 6);
    }
}
