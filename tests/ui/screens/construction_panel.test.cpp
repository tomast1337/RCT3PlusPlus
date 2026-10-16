// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/construction_panel.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>
#include <string>

using namespace rct3::ui;
namespace cp = rct3::ui::construction_panel;

namespace
{
    struct Fixture
    {
        Desktop desktop;
        ConstructionPanel* panel = nullptr;

        Fixture()
        {
            panel = &desktop.root().add<ConstructionPanel>("construction", game_skins::construction_panel_style(), u"Chain", u"Demolish");
            desktop.layout({ 0, 0, 1280, 720 });
            desktop.update({ });
        }

        void press(const Widget& w)
        {
            const Rect r = w.rect();
            const Point p { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
            desktop.update({ p, true });
            desktop.update({ p, false });
        }
    };

    /// Straight and level picked, the gentle curves and slopes offered, the rest not.
    ConstructionView straight()
    {
        ConstructionView v;
        v.curves.fill(IconState::Dis);
        v.slopes.fill(IconState::Dis);
        v.banks.fill(IconState::Dis);
        v.curves.at(3) = IconState::On;
        v.curves.at(2) = IconState::Off;
        v.curves.at(4) = IconState::Off;
        v.slopes.at(3) = IconState::On;
        v.slopes.at(4) = IconState::Off;
        v.banks.at(3) = IconState::On;
        v.piece = u"Straight";
        v.cost = u"Cost: 2";
        v.can_demolish = true;
        return v;
    }
} // namespace

TEST_CASE("a button's picture is its base name with the state after it")
{
    CHECK(icon_skin("CurveL1", IconState::On) == "CurveL1On");
    CHECK(icon_skin("CurveL1", IconState::Off) == "CurveL1Off");
    CHECK(icon_skin("CurveL1", IconState::Dis) == "CurveL1Dis");
}

TEST_CASE("the bank dial moves on a step for every two toggles")
{
    CHECK(dial_left(0) == cp::kToggleLeft);
    CHECK(dial_left(1) == cp::kToggleLeft + cp::kStep);
    CHECK(dial_left(2) == cp::kToggleLeft + cp::kStep);
    CHECK(dial_left(3) == cp::kToggleLeft + 2 * cp::kStep);
}

TEST_CASE("the bank buttons sit round the dial, level at the bottom and upside down at the top")
{
    const auto spots = bank_spots();
    const Point level = spots.at(3);
    const Point upside_down = spots.at(7);
    CHECK(level.x == upside_down.x);
    CHECK(level.y > upside_down.y);
    for (const Point p : spots)
    {
        CHECK(p.x >= 0);
        CHECK(p.y >= 0);
        CHECK(p.x < cp::kDialSize);
        CHECK(p.y + cp::kBankSize <= cp::kDialSize);
    }
    // Leaning left is drawn on the right, as a car is seen from the front.
    CHECK(spots.at(0).x > level.x);
    CHECK(spots.at(6).x < level.x);
}

TEST_CASE("the rows of curves and slopes sit side by side in the strip")
{
    Fixture f;
    const Rect panel = f.panel->rect();
    CHECK(panel.x0 == 0);
    CHECK(panel.y1 == 720);
    for (std::size_t i = 0; i + 1 < cp::kCurves; ++i)
    {
        CHECK(f.panel->curve_button(i + 1).rect().x0 - f.panel->curve_button(i).rect().x0 == cp::kStep);
        CHECK(f.panel->slope_button(i).rect().y0 > f.panel->curve_button(i).rect().y1 - 1);
    }
    CHECK(f.panel->curve_button(cp::kCurves - 1).rect().x1 <= panel.x0 + cp::kToggleLeft);
}

TEST_CASE("each button shows the picture of its state, banks not offered left out")
{
    Fixture f;
    const auto style = game_skins::construction_panel_style();
    f.panel->show(straight());
    CHECK(f.panel->curve_button(3).icon == icon_skin(style.curves.at(3), IconState::On));
    CHECK(f.panel->curve_button(0).icon == icon_skin(style.curves.at(0), IconState::Dis));
    CHECK(f.panel->slope_button(4).icon == icon_skin(style.slopes.at(4), IconState::Off));
    CHECK(f.panel->bank_button(3).visible);
    CHECK_FALSE(f.panel->bank_button(0).visible);
    CHECK(f.panel->piece_label().text == u"Straight");
    CHECK(f.panel->cost_label().text == u"Cost: 2");
}

TEST_CASE("the chain toggle shows only when the track has chain pieces, and moves the dial on")
{
    Fixture f;
    ConstructionView v = straight();
    f.panel->show(v);
    f.desktop.update({ });
    CHECK_FALSE(f.panel->chain_button().visible);
    const float without = f.panel->bank_button(3).rect().x0;
    v.chain = IconState::Off;
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->chain_button().visible);
    CHECK(f.panel->bank_button(3).rect().x0 - without == cp::kStep);
}

TEST_CASE("picking an offered value is heard; a value not offered is not")
{
    Fixture f;
    f.panel->show(straight());
    f.press(f.panel->curve_button(4));
    f.press(f.panel->curve_button(0));
    f.press(f.panel->slope_button(4));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Curve, 4 });
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Slope, 4 });
    CHECK_FALSE(f.panel->take_event());
}

TEST_CASE("picking the picked value again builds the piece")
{
    Fixture f;
    f.panel->show(straight());
    f.press(f.panel->curve_button(3));
    f.press(f.panel->bank_button(3));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Build, 0 });
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Build, 0 });
}

TEST_CASE("named pieces are picked, then built, by their buttons")
{
    Fixture f;
    ConstructionView v = straight();
    v.named = { u"Station", u"Loop" };
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->named_button(1).text == u"Loop");
    f.press(f.panel->named_button(1));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Named, 1 });
    v.named_on = 1;
    f.panel->show(v);
    f.press(f.panel->named_button(1));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Build, 0 });
    // With a named piece picked, the picked curve is no longer the piece to build.
    f.press(f.panel->curve_button(3));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Curve, 3 });
}

TEST_CASE("named pieces come a page at a time")
{
    Fixture f;
    ConstructionView v = straight();
    for (std::size_t i = 0; i < cp::kNamedRows * cp::kNamedColumns + 1; ++i)
        v.named.push_back(u"Piece " + std::u16string(1, static_cast<char16_t>(u'A' + i)));
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->page() == 0);
    CHECK_FALSE(f.panel->named_button(1).visible == false);
    f.press(*f.panel->find("page_down"));
    CHECK(f.panel->page() == 1);
    CHECK(f.panel->named_button(0).visible);
    CHECK(f.panel->named_button(0).text == v.named.back());
    CHECK_FALSE(f.panel->named_button(1).visible);
    f.press(f.panel->named_button(0));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Named, v.named.size() - 1 });
}

TEST_CASE("demolish is heard only while there is a piece to take down, and closing once")
{
    Fixture f;
    ConstructionView v = straight();
    f.panel->show(v);
    f.press(f.panel->demolish_button());
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Demolish, 0 });
    v.can_demolish = false;
    f.panel->show(v);
    f.press(f.panel->demolish_button());
    CHECK_FALSE(f.panel->take_event());
    f.press(*f.panel->find("close"));
    CHECK(f.panel->take_closed());
    CHECK_FALSE(f.panel->take_closed());
}
