// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/tickbox.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("a click flips the tick")
{
    Desktop desktop;
    Tickbox& t = desktop.root().add<Tickbox>("t", "Box", "Tick");
    t.placement = top_left(0, 0, 12, 12);
    desktop.layout({ 0, 0, 100, 100 });

    desktop.update({ { 5, 5 }, true, true });
    desktop.update({ { 5, 5 }, false, true });
    CHECK(t.ticked);
    CHECK(t.take_change());
    CHECK_FALSE(t.take_change());

    desktop.update({ { 5, 5 }, true, true });
    desktop.update({ { 50, 5 }, false, true });
    CHECK(t.ticked);
    CHECK_FALSE(t.take_change());
}
