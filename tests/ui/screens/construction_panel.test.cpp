// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/construction_panel.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>
#include <string>
#include <utility>

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
            panel = &desktop.root().add<ConstructionPanel>("construction", game_skins::construction_panel_style(kGamePalette), std::array<std::u16string, cp::kToggles> { u"Chain", u"Enclosed", u"Water", u"Wide", u"Booster" }, u"Demolish");
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

        /// Presses the middle of the list's row `row`, the list scrolled to its top.
        void press_row(std::size_t row)
        {
            const Rect r = panel->list().rect();
            const Point p { r.x0 + 20, r.y0 + (static_cast<float>(row) + 0.5f) * cp::kListRowHeight };
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
    CHECK(panel.y0 == 0); // Its owner places it
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
    const auto style = game_skins::construction_panel_style(kGamePalette);
    f.panel->show(straight());
    CHECK(f.panel->curve_button(3).icon == icon_skin(style.curves.at(3), IconState::On));
    CHECK(f.panel->curve_button(0).icon == icon_skin(style.curves.at(0), IconState::Dis));
    CHECK(f.panel->slope_button(4).icon == icon_skin(style.slopes.at(4), IconState::Off));
    CHECK(f.panel->bank_button(3).visible);
    CHECK_FALSE(f.panel->bank_button(0).visible);
    CHECK(f.panel->piece_label().text == u"Straight");
    CHECK(f.panel->cost_label().text == u"Cost: 2");
}

TEST_CASE("a toggle shows only when the track has pieces with it, and moves the dial on")
{
    Fixture f;
    ConstructionView v = straight();
    f.panel->show(v);
    f.desktop.update({ });
    CHECK_FALSE(f.panel->toggle_button(0).visible);
    const float without = f.panel->bank_button(3).rect().x0;
    v.toggles[0] = IconState::Off;
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->toggle_button(0).visible);
    CHECK(f.panel->bank_button(3).rect().x0 - without == cp::kStep);
}

TEST_CASE("toggles fill two rows by columns, and three push the pictures and the list right")
{
    Fixture f;
    ConstructionView v = straight();
    v.named = { { .name = u"Station", .icon = "StationIcon" }, { .name = u"Loop" } };
    f.panel->show(v);
    f.desktop.update({ });
    const float pictures = f.panel->find("special0")->rect().x0;
    const float list = f.panel->list().rect().x0;
    v.toggles = { IconState::Off, IconState::On, std::nullopt, std::nullopt, IconState::Off };
    f.panel->show(v);
    f.desktop.update({ });
    const Rect chain = f.panel->toggle_button(0).rect();
    const Rect tube = f.panel->toggle_button(1).rect();
    const Rect booster = f.panel->toggle_button(4).rect();
    CHECK(tube.x0 == chain.x0);
    CHECK(tube.y0 < chain.y0);
    CHECK(booster.x0 - chain.x0 == cp::kStep);
    CHECK(booster.y0 == chain.y0);
    CHECK(f.panel->find("special0")->rect().x0 - pictures == cp::kStep);
    CHECK(f.panel->list().rect().x0 - list == cp::kStep);
}

TEST_CASE("a toggle the picked piece cannot have is hidden in its place, and a click on one is heard")
{
    Fixture f;
    ConstructionView v = straight();
    v.toggles = { IconState::Dis, IconState::Off, std::nullopt, std::nullopt, std::nullopt };
    f.panel->show(v);
    f.desktop.update({ });
    CHECK_FALSE(f.panel->toggle_button(0).visible);
    f.press(f.panel->toggle_button(1));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Toggle, 1 });
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

TEST_CASE("named pieces are picked by a press on their row, and built by a second quick press")
{
    Fixture f;
    ConstructionView v = straight();
    v.named = { { .name = u"Station" }, { .name = u"Loop" } };
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->list().items == std::vector<std::u16string> { u"Loop", u"Station" });
    f.press_row(0);
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Named, 1 });
    v.named_on = 1;
    f.panel->show(v);
    CHECK(f.panel->list().selected() == 0);
    f.press_row(0);
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Build, 0 });
    // With a named piece picked, the picked curve is no longer the piece to build.
    f.press(f.panel->curve_button(3));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Curve, 3 });
}

TEST_CASE("the list shows the pieces that fit first, then by name, then by way")
{
    Fixture f;
    ConstructionView v = straight();
    v.named = { { .name = u"Zero G Roll" }, { .name = u"Helix", .marks = { "Right", "" }, .offered = false }, { .name = u"Corkscrew", .marks = { "Right", "" } }, { .name = u"Corkscrew", .marks = { "Left", "" } } };
    f.panel->show(v);
    CHECK(f.panel->list().items == std::vector<std::u16string> { u"Corkscrew", u"Corkscrew", u"Zero G Roll", u"Helix" });
    CHECK(f.panel->list().icons.at(0).front() == "Left");
    f.press_row(0);
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Named, 3 });
}

TEST_CASE("the list scrolls when its pieces do not all fit")
{
    Fixture f;
    ConstructionView v = straight();
    for (std::size_t i = 0; i < 12; ++i)
        v.named.push_back({ .name = u"Piece " + std::u16string(1, static_cast<char16_t>(u'A' + i)) });
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->list().items.size() == v.named.size());
    CHECK(f.panel->list().on_wheel({ 0, -1 }));
    f.desktop.update({ });
    CHECK(f.panel->list().bar().position > 0);
    // Picking keeps the scroll: showing the same view again does not pull the list back.
    const float scrolled = f.panel->list().bar().position;
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->list().bar().position == scrolled);
}

TEST_CASE("demolish is heard only while there is a piece to take down")
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
}

TEST_CASE("finishing the track is heard only while it has an open end")
{
    Fixture f;
    ConstructionView v = straight();
    f.panel->show(v);
    f.press(f.panel->complete_button());
    CHECK_FALSE(f.panel->take_event());
    v.can_complete = true;
    f.panel->show(v);
    f.press(f.panel->complete_button());
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Complete, 0 });
}

TEST_CASE("named pieces with a picture stand as picture buttons, out of the list")
{
    Fixture f;
    ConstructionView v = straight();
    v.named = { { .name = u"Station", .icon = "StationIcon" }, { .name = u"Loop" }, { .name = u"Brakes", .icon = "BrakesIcon" } };
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->special_button(0).visible);
    CHECK(f.panel->special_button(0).icon == "StationIcon");
    CHECK(f.panel->special_button(1).icon == "BrakesIcon");
    CHECK_FALSE(f.panel->special_button(2).visible);
    CHECK(f.panel->list().items == std::vector<std::u16string> { u"Loop" });
    f.press(f.panel->special_button(1));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Named, 2 });
    f.press_row(0);
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Named, 1 });
    v.named_on = 2;
    f.panel->show(v);
    CHECK(f.panel->special_button(1).on);
    CHECK(f.panel->special_button(1).icon_colour == kWhite);
    CHECK(f.panel->special_button(0).icon_colour != kWhite); // The others fade
    f.press(f.panel->special_button(1));
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Build, 0 });
}

TEST_CASE("a setting shows only for a piece that has it, a step of its size is heard, and the boxes end at the price's row's end")
{
    Fixture f;
    ConstructionView v = straight();
    f.panel->show(v);
    Spinner& speed = f.panel->param_spinner(PieceParam::Speed);
    CHECK_FALSE(speed.visible);
    v.params.at(std::to_underlying(PieceParam::Speed)) = ParamBox { .value = 400, .most = 3000, .decimals = 2, .unit = u"m/s", .step = 100 };
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(speed.visible);
    CHECK_FALSE(f.panel->param_spinner(PieceParam::CarTurn).visible);
    CHECK(speed.value() == 400);
    CHECK(speed.rect().x1 - f.panel->rect().x0 == doctest::Approx(cp::kParamsRight));
    f.press(speed); // Not on an arrow: nothing moves
    CHECK_FALSE(f.panel->take_event());
    const Rect up = speed.up_arrow();
    const Point p { (up.x0 + up.x1) / 2, (up.y0 + up.y1) / 2 };
    f.desktop.update({ p, true });
    f.desktop.update({ p, false });
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Param, std::to_underlying(PieceParam::Speed), 500 }); // A step at a time
    // A car turn goes before it and may go below nothing; the price's label stops short of both.
    v.params.at(std::to_underlying(PieceParam::CarTurn)) = ParamBox { .value = -90, .least = -720, .most = 720 };
    f.panel->show(v);
    f.desktop.update({ });
    Spinner& turn = f.panel->param_spinner(PieceParam::CarTurn);
    CHECK(turn.value() == -90);
    CHECK(turn.rect().x1 < speed.rect().x0);
    CHECK(f.panel->cost_label().rect().x1 < turn.rect().x0);
}

TEST_CASE("the cars' facing takes the next toggle slot and is heard")
{
    Fixture f;
    const auto style = game_skins::construction_panel_style(kGamePalette);
    ConstructionView v = straight();
    v.toggles = { IconState::Off, std::nullopt, std::nullopt, std::nullopt, std::nullopt };
    f.panel->show(v);
    f.desktop.update({ });
    CHECK_FALSE(f.panel->facing_button().visible);
    v.facing = 2;
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->facing_button().visible);
    CHECK(f.panel->facing_button().icon == style.facing + "3");
    CHECK(f.panel->facing_button().rect().x0 == f.panel->toggle_button(0).rect().x0); // The chain's column, the other row
    f.press(f.panel->facing_button());
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Facing, 0 });
}

TEST_CASE("a piece that cannot go on is greyed in its place, its marks too, and is not heard")
{
    Fixture f;
    ConstructionView v = straight();
    v.named = { { .name = u"Loop", .marks = { "LeftMark", "LoopMark" } }, { .name = u"Helix", .marks = { "", "HelixMark" }, .offered = false } };
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->list().items.at(1) == u"Helix");
    CHECK(f.panel->list().icons.at(0) == std::vector<std::string> { "LeftMark", "LoopMark" });
    CHECK(f.panel->list().greyed == std::vector<bool> { false, true });
    f.press_row(1);
    CHECK_FALSE(f.panel->take_event());
    CHECK_FALSE(f.panel->list().selected());
}

TEST_CASE("the heights button turns over, unless the view menu shows every ride's heights")
{
    Fixture f;
    const auto style = game_skins::construction_panel_style(kGamePalette);
    f.panel->heights_help = u"Heights";
    f.panel->heights_locked_help = u"Locked";
    ConstructionView v = straight();
    f.panel->show(v);
    CHECK(f.panel->heights_button().icon == style.heights);
    f.press(f.panel->heights_button());
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Heights, 0 });
    v.heights_locked = true;
    f.panel->show(v);
    CHECK(f.panel->heights_button().icon == style.heights_on);
    CHECK(f.panel->heights_button().tooltip == u"Locked");
    f.press(f.panel->heights_button());
    CHECK_FALSE(f.panel->take_event());
}

TEST_CASE("a new stretch button pushes the list right")
{
    Fixture f;
    ConstructionView v = straight();
    f.panel->show(v);
    const float left = f.panel->list().rect().x0;
    v.new_segment = true;
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->list().rect().x0 > left);
    CHECK(f.panel->new_segment_button().rect().x1 < f.panel->list().rect().x0);
}

TEST_CASE("the split button shows the picture of its state, right of the new stretch button, and is heard")
{
    Fixture f;
    const auto style = game_skins::construction_panel_style(kGamePalette);
    ConstructionView v = straight();
    f.panel->show(v);
    CHECK_FALSE(f.panel->split_button().visible);
    v.split = 2;
    v.new_segment = true;
    f.panel->show(v);
    f.desktop.update({ });
    CHECK(f.panel->split_button().icon == style.split.at(2));
    CHECK(f.panel->split_button().rect().x0 > f.panel->new_segment_button().rect().x1);
    CHECK(f.panel->split_button().rect().x1 < f.panel->list().rect().x0);
    f.press(f.panel->split_button());
    CHECK(f.panel->take_event() == ConstructionEvent { ConstructionAction::Split, 0 });
}
