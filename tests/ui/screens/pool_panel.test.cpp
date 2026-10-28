// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/pool_panel.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        StringTable strings;
        Desktop desktop;
        PoolPanel* panel = nullptr;

        Fixture()
        {
            panel = &desktop.root().add<PoolPanel>("pools", game_skins::pool_panel_style(kGamePalette), strings);
            desktop.layout({ 0, 0, 1000, 700 });
            desktop.update({ });
            panel->show({ { "lamp", u"Lamp" }, { "ladder", u"Ladder" } }, { { "room", u"Room" } });
            desktop.update({ });
        }

        Button& button(std::string_view path) const
        {
            return *desktop.root().find_as<Button>(path);
        }

        void click(std::string_view path)
        {
            const Rect r = button(path).rect();
            PointerState s;
            s.position = { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
            desktop.update(s);
            s.down = true;
            desktop.update(s);
            s.down = false;
            desktop.update(s);
        }
    };
} // namespace

TEST_CASE("the pool panel opens on the editor with water out")
{
    Fixture f;
    CHECK(f.panel->showing());
    CHECK(f.panel->page() == PoolPage::Editor);
    CHECK(f.panel->take_change());
    CHECK(f.panel->editor().mode == PoolMode::Water);
    CHECK(f.panel->editor().snap);
    CHECK_FALSE(f.button("pools/Pool_PathPanel").enabled);
}

TEST_CASE("an edge button sets the edge and puts the water out")
{
    Fixture f;
    f.click("pools/body/editor/Pool_PlaceTiles");
    (void)f.panel->take_change();
    f.click("pools/body/editor/Pool_CurveLarge");
    CHECK(f.panel->take_change());
    CHECK(f.panel->editor().edge == 3);
    CHECK(f.panel->editor().mode == PoolMode::Water);
}

TEST_CASE("the editor's buttons change the tool and keep their choices")
{
    Fixture f;
    (void)f.panel->take_change();
    f.click("pools/body/editor/Pool_DeleteArea");
    CHECK(f.panel->take_change());
    CHECK(f.panel->editor().mode == PoolMode::Delete);
    f.click("pools/body/editor/Pool_DepthDeep");
    CHECK(f.panel->editor().depth == 3);
    f.panel->hide();
    f.panel->show({ }, { });
    CHECK(f.panel->editor().mode == PoolMode::Delete);
    CHECK(f.panel->editor().depth == 3);
}

TEST_CASE("an extra picked replaces the editor's tool until the editor's tab opens again")
{
    Fixture f;
    f.click("pools/Pool_ExtraPanel");
    CHECK(f.panel->page() == PoolPage::Extras);
    (void)f.panel->take_change();
    f.click("pools/body/ladder");
    CHECK(f.panel->take_change());
    CHECK(f.panel->placing() == std::pair { PoolPage::Extras, std::size_t { 1 } });
    f.click("pools/Pool_PoolPanel");
    CHECK(f.panel->take_change());
    CHECK_FALSE(f.panel->placing());
}
