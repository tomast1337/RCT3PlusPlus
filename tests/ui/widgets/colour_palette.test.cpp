// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/colour_palette.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    ColourPaletteStyle style()
    {
        ColourPaletteStyle s;
        s.swatch_prefix = "Paint";
        s.ring = "Ring";
        s.current_ring = "Marked";
        s.swatch = 10;
        s.pitch = 12;
        s.margin = 5;
        return s;
    }

    PointerState at(float x, float y, bool down = false)
    {
        return { { x, y }, down, true };
    }
} // namespace

TEST_CASE("a palette's swatches stand in rows from the top left, the board fitting round them")
{
    const Point size = ColourPalette::size(style(), 10, 4);
    CHECK(size.x == 5 + 3 * 12 + 10 + 5);
    CHECK(size.y == 5 + 2 * 12 + 10 + 5);
    CHECK(ColourPalette::size(style(), 2, 4).x == 5 + 12 + 10 + 5);

    Desktop desktop;
    ColourPalette& p = desktop.root().add<ColourPalette>("p", style(), 10, 4, 6);
    p.placement = top_left(100, 50, size.x, size.y);
    desktop.layout({ 0, 0, 400, 300 });
    REQUIRE(p.count() == 10);
    CHECK(p.swatch(0).rect().x0 == 105);
    CHECK(p.swatch(0).rect().y0 == 55);
    CHECK(p.swatch(5).rect().x0 == 105 + 12);
    CHECK(p.swatch(5).rect().y0 == 55 + 12);
    CHECK(p.swatch(3).icon == "Paint3");
    // The colour picked before stands out.
    CHECK(p.swatch(6).skin == "Marked");
    CHECK(p.swatch(7).skin == "Ring");
}

TEST_CASE("clicking a swatch picks its colour once")
{
    Desktop desktop;
    ColourPalette& p = desktop.root().add<ColourPalette>("p", style(), 8, 4, 0);
    p.placement = top_left(0, 0, 60, 40);
    desktop.layout({ 0, 0, 200, 200 });
    CHECK_FALSE(p.take_pick());
    const Rect r = p.swatch(6).rect();
    desktop.update(at(r.x0 + 2, r.y0 + 2, true));
    desktop.update(at(r.x0 + 2, r.y0 + 2, false));
    CHECK(p.take_pick() == 6);
    CHECK_FALSE(p.take_pick());
    desktop.update(at(r.x0 + 2, r.y0 + 2, true));
    desktop.update(at(r.x0 + 40, r.y0 + 2, false)); // Let go elsewhere
    CHECK_FALSE(p.take_pick());
}
