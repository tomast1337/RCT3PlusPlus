// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/paint_button.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    PaintButtonStyle style()
    {
        PaintButtonStyle s;
        s.pots = { "One", "Two", "Three" };
        s.lit = { { { "One0", { }, { } }, { "Two0", "Two1", { } }, { "Three0", "Three1", "Three2" } } };
        // On a picture 20 across: one pot in the middle; two side by side; three with one on top.
        s.spots = { {
            { { { 10, 10 }, 8 } },
            { { { 5, 10 }, 6 }, { { 15, 10 }, 6 } },
            { { { 10, 5 }, 4 }, { { 5, 15 }, 4 }, { { 15, 15 }, 4 } },
        } };
        s.paint_prefix = "Paint";
        s.icon_size = 20;
        return s;
    }

    struct Fixture
    {
        Desktop desktop;
        PaintButton& button;

        explicit Fixture(std::vector<std::size_t> colours)
            : button(desktop.root().add<PaintButton>("paint", style(), std::move(colours)))
        {
            button.placement = top_left(100, 100, 20, 20);
            desktop.layout({ 0, 0, 400, 400 });
        }

        void press(Point p, bool down)
        {
            desktop.update({ p, down, true });
        }
    };
} // namespace

TEST_CASE("a click on a paint button tells which pot it was on")
{
    Fixture f({ 3, 7, 9 });
    CHECK(f.button.pot_at({ 110, 104 }) == 0);
    CHECK(f.button.pot_at({ 103, 117 }) == 1);
    CHECK(f.button.pot_at({ 117, 116 }) == 2);

    f.press({ 116, 115 }, true);
    f.press({ 116, 115 }, false);
    CHECK(f.button.take_pot() == 2);
    CHECK_FALSE(f.button.take_pot());

    // A click from the keys paints the first pot.
    f.button.click();
    CHECK(f.button.take_pot() == 0);
}

TEST_CASE("the pot under the pointer lights up, or the one held while its colours are picked")
{
    Fixture f({ 3, 7 });
    f.press({ 116, 110 }, false);
    CHECK(f.button.lit() == 1);
    CHECK(f.button.icon == "Two1");
    f.press({ 300, 300 }, false);
    CHECK_FALSE(f.button.lit());
    CHECK(f.button.icon == "Two");
    f.button.held = 0;
    f.press({ 300, 300 }, false);
    CHECK(f.button.icon == "Two0");
}

TEST_CASE("a paint button has one to three pots")
{
    CHECK(Fixture({ }).button.colours.size() == 1);
    CHECK(Fixture({ 1, 2, 3, 4 }).button.colours.size() == 3);
}
