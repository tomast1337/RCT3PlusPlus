// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tool_column.hpp"

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
        ToolColumn* column = nullptr;

        explicit Fixture(float height)
        {
            strings.add("MapHelp", u"Karten");
            strings.add("File_QuitHelp", u"Ende");
            strings.add("File_CareerMapHelp", u"Karriere");
            column = &desktop.root().add<ToolColumn>("tools", game_skins::tool_column_style(), strings);
            desktop.layout({ 0, 0, 1000, height });
            desktop.update({ });
        }
    };
} // namespace

TEST_CASE("the column's step is fixed on tall screens and shrinks on short ones")
{
    CHECK(tool_column_step(1080) == tool_column::kButtonSize);
    CHECK(tool_column_step(700) == tool_column::kButtonSize);
    CHECK(tool_column_step(600) < tool_column::kButtonSize);
    CHECK(tool_column_step(450) < tool_column_step(499));
    CHECK(tool_column_step(100) == 25);
}

TEST_CASE("the buttons stack up from the bottom left, with a gap before the leave button")
{
    Fixture f(800);
    const Rect first = f.column->button(ParkTool::Delete).rect();
    CHECK(first.x0 == tool_column::kMargin);
    CHECK(first.y1 == 800 - tool_column::kMargin);
    CHECK(first.width() == tool_column::kButtonSize);
    const Rect management = f.column->button(ParkTool::Management).rect();
    const Rect rides = f.column->button(ParkTool::Rides).rect();
    const Rect leave = f.column->button(ParkTool::Leave).rect();
    CHECK(rides.y0 - management.y0 == tool_column::kButtonSize);
    CHECK(management.y0 - leave.y0 == tool_column::kButtonSize * 1.5f);
    CHECK(f.column->button(ParkTool::Map).rect().y0 > 0);
    CHECK(f.column->button(ParkTool::Map).tooltip == u"Karten");
}

TEST_CASE("all buttons fit on a short screen")
{
    Fixture f(360);
    const Rect top = f.column->button(ParkTool::Map).rect();
    CHECK(top.y0 >= 0);
    CHECK(top.width() < tool_column::kButtonSize);
}

TEST_CASE("a press is reported once, and the leave button follows the career")
{
    Fixture f(800);
    const Rect r = f.column->button(ParkTool::Rides).rect();
    const Point p { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
    f.desktop.update({ p, true });
    f.desktop.update({ p, false });
    CHECK(f.column->take_pressed() == ParkTool::Rides);
    CHECK_FALSE(f.column->take_pressed());

    CHECK(f.column->button(ParkTool::Leave).icon == "File_Quit");
    CHECK(f.column->button(ParkTool::Leave).tooltip == u"Ende");
    f.column->set_career(true);
    CHECK(f.column->button(ParkTool::Leave).icon == "File_CareerMap");
    CHECK(f.column->button(ParkTool::Leave).tooltip == u"Karriere");
}
