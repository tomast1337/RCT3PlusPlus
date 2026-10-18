// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tool_row.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/tool_menus.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <vector>

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

        void press(const Widget& w)
        {
            const Rect r = w.rect();
            const Point p { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
            desktop.update({ p, true });
            desktop.update({ p, false });
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

TEST_CASE("a press of a button without a menu is reported once, and the leave button follows the career")
{
    Fixture f(1000, 800);
    f.press(f.row->button(ParkTool::Map));
    CHECK(f.row->take_pressed() == ParkTool::Map);
    CHECK_FALSE(f.row->take_pressed());
    CHECK_FALSE(f.row->open_menu());

    CHECK(f.row->button(ParkTool::Leave).icon == "File_Quit");
    CHECK(f.row->button(ParkTool::Leave).tooltip == u"Ende");
    f.row->set_career(true);
    CHECK(f.row->button(ParkTool::Leave).icon == "File_CareerMap");
    CHECK(f.row->button(ParkTool::Leave).tooltip == u"Karriere");
}

TEST_CASE("a tool's menu opens in a row above its button, one at a time, and closes when pressed again")
{
    Fixture f(1000, 800);
    CHECK_FALSE(f.row->item(ToolItem::Coasters).visible);
    f.press(f.row->button(ParkTool::Rides));
    CHECK(f.row->open_menu() == ParkTool::Rides);
    CHECK_FALSE(f.row->take_pressed()); // Opening a menu is not a press for the owner
    const Rect button = f.row->button(ParkTool::Rides).rect();
    const Rect first = f.row->item(tool_menu(ParkTool::Rides).front().item).rect();
    const Rect last = f.row->item(ToolItem::Coasters).rect();
    CHECK(first.y1 == button.y0 - tool_row::kItemGap);
    CHECK(last.y0 == first.y0);
    CHECK(last.x0 - first.x0 == tool_row::kItemStep * 11);
    CHECK((first.x0 + last.x1) / 2 == doctest::Approx((button.x0 + button.x1) / 2).epsilon(0.02));

    f.press(f.row->button(ParkTool::View));
    CHECK(f.row->open_menu() == ParkTool::View);
    CHECK_FALSE(f.row->item(ToolItem::Coasters).visible);
    CHECK(f.row->item(ToolItem::Grid).visible);
    f.press(f.row->button(ParkTool::View));
    CHECK_FALSE(f.row->open_menu());
    CHECK_FALSE(f.row->item(ToolItem::Grid).visible);
}

TEST_CASE("a menu stays on the screen, and one too wide for it goes on in another row above")
{
    Fixture f(1000, 800);
    f.press(f.row->button(ParkTool::Delete));
    CHECK(f.row->item(ToolItem::DeleteOne).rect().x0 >= tool_row::kMargin);
    f.press(f.row->button(ParkTool::View));
    CHECK(f.row->item(ToolItem::Grid).rect().x1 <= 1000 - tool_row::kMargin);

    Fixture narrow(320, 600);
    narrow.press(narrow.row->button(ParkTool::Rides));
    const Rect first = narrow.row->item(tool_menu(ParkTool::Rides).front().item).rect();
    const Rect last = narrow.row->item(ToolItem::Coasters).rect();
    CHECK(last.y1 < first.y0);
    CHECK(first.x0 >= 0);
    CHECK(last.x1 <= 320);
}

TEST_CASE("picking an item reports it and leaves the menu open; switches turn over and change their icon")
{
    Fixture f(1000, 800);
    f.press(f.row->button(ParkTool::View));
    CHECK(f.row->switched_on(ToolItem::People));
    const std::string shown = f.row->item(ToolItem::People).icon;
    f.press(f.row->item(ToolItem::People));
    CHECK(f.row->take_item() == ToolItem::People);
    CHECK_FALSE(f.row->take_item());
    CHECK(f.row->open_menu() == ParkTool::View);
    CHECK_FALSE(f.row->switched_on(ToolItem::People));
    CHECK(f.row->item(ToolItem::People).icon != shown);
    f.row->set_switch(ToolItem::People, true);
    CHECK(f.row->item(ToolItem::People).icon == shown);

    // An item that cannot be used yet is not reported.
    f.row->set_available(ToolItem::Grid, false);
    f.press(f.row->item(ToolItem::Grid));
    CHECK_FALSE(f.row->take_item());
}

TEST_CASE("every tool but paths, the map and leaving has a menu, and no item is in two")
{
    std::vector<ToolItem> all;
    for (std::size_t t = 0; t < kParkToolCount; ++t)
    {
        const auto tool = static_cast<ParkTool>(t);
        const bool acts_at_once = tool == ParkTool::Paths || tool == ParkTool::Map || tool == ParkTool::Leave;
        CHECK(tool_menu(tool).empty() == acts_at_once);
        for (const ToolMenuItem& i : tool_menu(tool))
        {
            CHECK_FALSE(std::ranges::contains(all, i.item));
            all.push_back(i.item);
            CHECK(is_switch(i.item) == !i.shown_icon.empty());
        }
    }
}

TEST_CASE("each tool holds its button and its menu's items, named by their icons")
{
    Fixture f(1000, 800);
    const Widget& view = *f.desktop.root().find("tools/View");
    CHECK(f.row->button(ParkTool::View).parent() == &view);
    CHECK(f.row->item(ToolItem::People).parent() == &view);
    CHECK(f.desktop.root().find("tools/View/Land_PeopleOff") == &f.row->item(ToolItem::People));
    CHECK(f.row->item(ToolItem::DeleteOne).parent()->name() == "Delete");
}
