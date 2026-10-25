// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/delete_area_panel.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        StringTable strings;
        Desktop desktop;
        DeleteAreaPanel* panel = nullptr;

        Fixture()
        {
            panel = &desktop.root().add<DeleteAreaPanel>("delete_area", game_skins::delete_area_panel_style(), strings);
            frame();
        }

        void frame(Point at = { 500, 10 }, bool down = false)
        {
            desktop.layout({ 0, 0, 1000, 700 });
            desktop.update({ at, down }, { }, { .elapsed = 0 });
        }

        void click(std::string_view name)
        {
            const Rect r = panel->find(name)->rect();
            const Point p { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
            frame(p, true);
            frame(p, false);
        }
    };
} // namespace

TEST_CASE("the area panel's buttons do nothing until tiles are marked")
{
    Fixture f;
    f.click("GUI2_DeleteSelection_DeleteIcon");
    CHECK_FALSE(f.panel->take_action());
}

TEST_CASE("the area panel tells which button was pressed, once")
{
    Fixture f;
    f.panel->set_marked(true);
    f.click("GUI2_DeleteSelection_ClearIcon");
    CHECK(f.panel->take_action() == DeleteAreaAction::Clear);
    CHECK_FALSE(f.panel->take_action());
    f.click("GUI2_DeleteSelection_DeleteIcon");
    CHECK(f.panel->take_action() == DeleteAreaAction::Delete);
}
