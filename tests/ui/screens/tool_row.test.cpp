// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tool_row.hpp"

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
        ToolRow* row = nullptr;

        Fixture(float width, float height)
        {
            strings.add("MapHelp", u"Karten");
            strings.add("File_QuitHelp", u"Ende");
            strings.add("File_CareerMapHelp", u"Karriere");
            row = &desktop.root().add<ToolRow>("tools", game_skins::tool_row_style(), strings);
            desktop.layout({ 0, 0, width, height });
            desktop.update({ });
        }
    };
} // namespace

TEST_CASE("the row's step is fixed on wide screens and shrinks on narrow ones")
{
    CHECK(tool_row_step(1920) == tool_row::kButtonSize);
    CHECK(tool_row_step(640) == tool_row::kButtonSize);
    CHECK(tool_row_step(400) < tool_row::kButtonSize);
    CHECK(tool_row_step(300) < tool_row_step(350));
    CHECK(tool_row_step(10) == 16);
}

TEST_CASE("the buttons sit in a row in the middle of the bottom edge, with a gap before the leave button")
{
    Fixture f(1000, 800);
    const Rect first = f.row->button(ParkTool::Delete).rect();
    const Rect last = f.row->button(ParkTool::Map).rect();
    CHECK(first.y1 == 800 - tool_row::kMargin);
    CHECK(last.y1 == first.y1);
    CHECK(first.width() == tool_row::kButtonSize);
    CHECK(first.x0 + last.x1 == doctest::Approx(1000).epsilon(0.002));
    const Rect management = f.row->button(ParkTool::Management).rect();
    const Rect rides = f.row->button(ParkTool::Rides).rect();
    const Rect leave = f.row->button(ParkTool::Leave).rect();
    CHECK(management.x0 - rides.x0 == tool_row::kButtonSize);
    CHECK(leave.x0 - management.x0 == tool_row::kButtonSize * 1.5f);
    CHECK(f.row->button(ParkTool::Map).tooltip == u"Karten");

    // Still in the middle once the window is wider.
    f.desktop.layout({ 0, 0, 1600, 800 });
    f.desktop.update({ });
    CHECK(f.row->button(ParkTool::Delete).rect().x0 + f.row->button(ParkTool::Map).rect().x1 == doctest::Approx(1600).epsilon(0.002));
}

TEST_CASE("all buttons fit on a narrow screen")
{
    Fixture f(320, 600);
    CHECK(f.row->button(ParkTool::Delete).rect().x0 >= 0);
    CHECK(f.row->button(ParkTool::Map).rect().x1 <= 320);
    CHECK(f.row->button(ParkTool::Map).rect().width() < tool_row::kButtonSize);
}

TEST_CASE("a press is reported once, and the leave button follows the career")
{
    Fixture f(1000, 800);
    const Rect r = f.row->button(ParkTool::Rides).rect();
    const Point p { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
    f.desktop.update({ p, true });
    f.desktop.update({ p, false });
    CHECK(f.row->take_pressed() == ParkTool::Rides);
    CHECK_FALSE(f.row->take_pressed());

    CHECK(f.row->button(ParkTool::Leave).icon == "File_Quit");
    CHECK(f.row->button(ParkTool::Leave).tooltip == u"Ende");
    f.row->set_career(true);
    CHECK(f.row->button(ParkTool::Leave).icon == "File_CareerMap");
    CHECK(f.row->button(ParkTool::Leave).tooltip == u"Karriere");
}
