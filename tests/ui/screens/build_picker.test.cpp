// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/build_picker.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        StringTable strings;
        Desktop desktop;
        BuildPicker* picker = nullptr;

        explicit Fixture(std::vector<PickerRow> rows, std::vector<PickerTheme> themes = { })
        {
            picker = &desktop.root().add<BuildPicker>("list", game_skins::build_picker_style(), strings, u"Things", "ThingsIcon", std::move(rows), std::move(themes));
            picker->placement = top_left(10, 10, build_picker::kWidth, build_picker::kHeight);
            step();
        }

        void step()
        {
            desktop.layout({ 0, 0, 800, 600 });
            desktop.update({ });
            desktop.layout({ 0, 0, 800, 600 });
        }

        void press(const Widget& w)
        {
            const Rect r = w.rect();
            const Point p { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
            desktop.update({ p, true });
            desktop.update({ p, false });
            step();
        }
    };

    PickerRow row(std::u16string name, std::size_t item, std::vector<PickerRow> children = { })
    {
        PickerRow r;
        r.name = std::move(name);
        r.item = item;
        r.children = std::move(children);
        return r;
    }
} // namespace

TEST_CASE("picking a row marks it and gives its item once")
{
    Fixture f({ row(u"One", 7), row(u"Two", 9) });
    REQUIRE(f.picker->rows().size() == 2);
    const BuildPickerRow& two = *f.picker->rows()[1];
    CHECK(two.rect().x0 == f.picker->rows()[0]->rect().x0 + build_picker::kTile);
    f.press(two);
    CHECK(f.picker->take_picked() == 9u);
    CHECK_FALSE(f.picker->take_picked().has_value());
    CHECK(two.picked);
    CHECK_FALSE(f.picker->rows()[0]->picked);
    CHECK(two.tooltip == u"Two");
}

TEST_CASE("an item's help tells its name, size and price, a line each")
{
    PickerRow r = row(u"Carousel", 1);
    r.size = u"3 x 3";
    r.price = u"$500";
    Fixture f({ r, row(u"Free", 2) });
    CHECK(f.picker->rows()[0]->tooltip == u"Carousel\n3 x 3\n$500");
    CHECK(f.picker->rows()[1]->tooltip == u"Free");
}

TEST_CASE("a heading opens to show its rows and is not picked")
{
    Fixture f({ row(u"Set", 1, { row(u"Piece", 2) }), row(u"After", 3) });
    REQUIRE(f.picker->rows().size() == 3);
    BuildPickerRow& heading = *f.picker->rows()[0];
    BuildPickerRow& piece = *f.picker->rows()[1];
    BuildPickerRow& after = *f.picker->rows()[2];
    CHECK_FALSE(piece.visible);
    CHECK(after.rect().x0 == heading.rect().x0 + build_picker::kTile);
    f.press(heading);
    CHECK_FALSE(f.picker->take_picked().has_value());
    CHECK(heading.open);
    CHECK(piece.visible);
    CHECK(piece.rect().x0 == heading.rect().x0 + build_picker::kTile);
    CHECK(after.rect().x0 == heading.rect().x0 + 2 * build_picker::kTile);
}

TEST_CASE("the items fill as many columns as the panel is wide")
{
    std::vector<PickerRow> rows;
    for (std::size_t i = 0; i < 9; ++i)
        rows.push_back(row(u"Item", i));
    Fixture f(std::move(rows));
    f.step();
    const auto& tiles = f.picker->rows();
    const auto columns = static_cast<std::size_t>(tiles[0]->parent()->rect().width() / build_picker::kTile);
    REQUIRE(columns >= 2);
    CHECK(tiles[columns - 1]->rect().y0 == tiles[0]->rect().y0);
    CHECK(tiles[columns]->rect().x0 == tiles[0]->rect().x0);
    CHECK(tiles[columns]->rect().y0 == tiles[0]->rect().y0 + build_picker::kTile);
}

TEST_CASE("items dearer than the cash are marked; none when money never runs out")
{
    PickerRow cheap = row(u"Cheap", 1);
    cheap.cost = 500;
    PickerRow dear = row(u"Dear", 2);
    dear.cost = 5000;
    Fixture f({ cheap, dear });
    CHECK_FALSE(f.picker->rows()[1]->dear);
    f.picker->cash = 1000;
    f.step();
    CHECK_FALSE(f.picker->rows()[0]->dear);
    CHECK(f.picker->rows()[1]->dear);
}

TEST_CASE("the search box keeps the items whose names hold it, and their headings open")
{
    Fixture f({ row(u"Carousel", 1), row(u"Café set", 2, { row(u"Café sign", 3), row(u"Bench", 4) }), row(u"Ferris wheel", 5) });
    const auto& rows = f.picker->rows();
    f.picker->search_box().text = u"CAFE";
    f.step();
    CHECK_FALSE(rows[0]->visible);
    CHECK(rows[1]->visible);
    CHECK(rows[2]->visible);
    CHECK_FALSE(rows[3]->visible);
    CHECK_FALSE(rows[4]->visible);
    f.picker->search_box().text = u"wheel";
    f.step();
    CHECK_FALSE(rows[1]->visible);
    CHECK(rows[4]->visible);
    CHECK(rows[4]->rect().y0 == f.picker->rows()[4]->parent()->rect().y0);
    f.desktop.update({ }, KeyboardState { .presses = { KeyPress { .key = Key::Escape } } });
    f.step();
    CHECK(f.picker->search_box().text.empty());
    CHECK(rows[0]->visible);
    f.desktop.update({ }, KeyboardState { .presses = { KeyPress { .key = Key::Escape } } });
    f.step();
    CHECK(!f.desktop.root().find("list"));
}

TEST_CASE("theme filters show while on and hide their theme's items while off")
{
    PickerRow moon = row(u"Moon rocket", 1);
    moon.style = "Moon";
    PickerRow sea = row(u"Sea shell", 2);
    sea.style = "Sea";
    // A theme none of the items belong to still gets its filter, in the order given.
    Fixture f({ moon, sea, row(u"Plain", 3) }, { { "Moon", u"Lunar" }, { "Sea", u"Ocean" }, { "Dust", u"Desert" } });
    REQUIRE(f.picker->filters().size() == 3);
    CHECK(f.picker->filters()[2]->name() == "Dust");
    Button& moon_filter = *f.picker->filters()[0];
    CHECK(moon_filter.name() == "Moon");
    CHECK(moon_filter.on);
    CHECK(moon_filter.tooltip.starts_with(u"Lunar\n"));
    f.press(moon_filter);
    CHECK_FALSE(moon_filter.on);
    CHECK(f.picker->styles_off.contains("Moon"));
    CHECK_FALSE(f.picker->rows()[0]->visible);
    CHECK(f.picker->rows()[1]->visible);
    CHECK(f.picker->rows()[2]->visible); // No theme: always shown
}

TEST_CASE("a pack filter that is on keeps only that pack's items")
{
    PickerRow wild = row(u"Lion", 1);
    wild.addon = 2;
    Fixture f({ wild, row(u"Bench", 2) }, { { "Moon", u"Lunar" } });
    REQUIRE(f.picker->filters().size() == 1); // One theme or none: no theme filters
    Button& pack = *f.picker->filters()[0];
    CHECK(pack.name() == "pack1");
    CHECK_FALSE(pack.on);
    CHECK(f.picker->rows()[1]->visible);
    f.press(pack);
    CHECK(f.picker->packs_only.contains(2u));
    CHECK(f.picker->rows()[0]->visible);
    CHECK_FALSE(f.picker->rows()[1]->visible);
}

TEST_CASE("filters set by the owner before the first update hold")
{
    PickerRow moon = row(u"Moon rocket", 1);
    moon.style = "Moon";
    PickerRow sea = row(u"Sea shell", 2);
    sea.style = "Sea";
    StringTable strings;
    Desktop desktop;
    auto& picker = desktop.root().add<BuildPicker>("list", game_skins::build_picker_style(), strings, u"Things", "ThingsIcon", std::vector { moon, sea }, std::vector<PickerTheme> { { "Moon", u"Lunar" }, { "Sea", u"Ocean" } });
    picker.styles_off = { "Sea" };
    desktop.layout({ 0, 0, 800, 600 });
    desktop.update({ });
    desktop.layout({ 0, 0, 800, 600 });
    CHECK(picker.rows()[0]->visible);
    CHECK_FALSE(picker.rows()[1]->visible);
    CHECK_FALSE(picker.filters()[1]->on);
}

TEST_CASE("object filters come first and hide their kind's items while off")
{
    PickerRow steel = row(u"Steel", 1);
    steel.kind = "Coaster_Steel";
    PickerRow wood = row(u"Wood", 2);
    wood.kind = "Coaster_Wooden";
    PickerRow moon = row(u"Moon", 3);
    moon.style = "Moon";
    PickerRow sea = row(u"Sea", 4);
    sea.style = "Sea";
    Fixture f({ steel, wood, moon, sea }, { { "Moon", u"Lunar" }, { "Sea", u"Ocean" } });
    REQUIRE(f.picker->filters().size() == 4);
    Button& steel_filter = *f.picker->filters()[0];
    CHECK(steel_filter.name() == "Coaster_Steel");
    CHECK(f.picker->filters()[2]->name() == "Moon");
    CHECK(f.picker->filters()[2]->placement.offsets.x0 == 2 * build_picker::kFilter + build_picker::kGroupGap);
    f.press(steel_filter);
    CHECK_FALSE(steel_filter.on);
    CHECK(steel_filter.icon == "ObjectFilter_Grey_Coaster_Steel");
    CHECK_FALSE(f.picker->rows()[0]->visible);
    CHECK(f.picker->rows()[1]->visible);
    CHECK(f.picker->rows()[2]->visible); // No kind: always shown
}
