// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/ride_camera_panel.hpp"

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
        RideCameraPanel* panel = nullptr;

        Fixture()
        {
            strings.add("RidecamGUI_DisabledTrain", u"Nur ein Zug");
            strings.add("RidecamGUI_Type", u"Zur #-Kamera");
            panel = &desktop.root().add<RideCameraPanel>("ride_camera", game_skins::ride_camera_panel_style(kGamePalette), strings);
            frame();
        }

        void frame(float seconds = 0, Point at = { 10, 10 }, bool down = false)
        {
            desktop.layout({ 0, 0, 1000, 700 });
            desktop.update({ at, down }, { }, { .elapsed = seconds });
        }

        void click(const Widget& w)
        {
            const Point p { (w.rect().x0 + w.rect().x1) / 2, (w.rect().y0 + w.rect().y1) / 2 };
            frame(0, p, true);
            frame(0, p, false);
        }
    };
} // namespace

TEST_CASE("the ride camera panel hides once the pointer is still, and comes back when it moves")
{
    Fixture f;
    CHECK(f.panel->rect().y1 == doctest::Approx(700));
    for (int i = 0; i < 10; ++i)
        f.frame(0.5f);
    CHECK(f.panel->shown() == 0);
    f.frame(0);
    CHECK(f.panel->rect().y0 >= 700); // Out of sight below the screen
    f.panel->pointer_moved(50);
    for (int i = 0; i < 10; ++i)
        f.frame(0.1f);
    CHECK(f.panel->shown() == 1);

    // Pinned, it stays.
    f.click(f.panel->pin());
    CHECK(f.panel->pinned());
    for (int i = 0; i < 10; ++i)
        f.frame(1);
    CHECK(f.panel->shown() == 1);
}

TEST_CASE("the ride camera panel's buttons ask for steps, greyed out where there is nowhere to go")
{
    Fixture f;
    f.panel->show(u"High Flier", true, false);
    CHECK(f.panel->info().text == u"High Flier");
    CHECK(f.panel->button(RideCameraAction::NextCamera).enabled);
    CHECK_FALSE(f.panel->button(RideCameraAction::NextTrain).enabled);
    CHECK(f.panel->button(RideCameraAction::NextTrain).tooltip == u"Nur ein Zug");
    f.click(f.panel->button(RideCameraAction::NextCamera));
    CHECK(f.panel->take_action() == RideCameraAction::NextCamera);
    CHECK_FALSE(f.panel->take_action());
    CHECK_FALSE(f.panel->button(RideCameraAction::ChangeType).enabled);
    f.panel->show(u"High Flier", true, false, 2, u"Heck");
    CHECK(f.panel->button(RideCameraAction::ChangeType).tooltip == u"Zur Heck-Kamera");
    CHECK(f.panel->button(RideCameraAction::ChangeType).icon == "RideCamGUI_Rear");
    f.click(f.panel->button(RideCameraAction::ChangeType));
    CHECK(f.panel->take_action() == RideCameraAction::ChangeType);
    f.click(f.panel->button(RideCameraAction::Leave));
    CHECK(f.panel->take_action() == RideCameraAction::Leave);
}
