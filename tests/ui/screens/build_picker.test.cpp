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

        explicit Fixture(std::vector<PickerRow> rows)
        {
            strings.add("RideSelectionWindow_ExpandAll", u"Bigger");
            strings.add("RideSelectionWindow_ContractAll", u"Smaller");
            picker = &desktop.root().add<BuildPicker>("list", game_skins::build_picker_style(), strings, u"Things", std::move(rows), true);
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
    CHECK(two.rect().y0 == f.picker->rows()[0]->rect().y0 + build_picker::kLargeRow);
    f.press(two);
    CHECK(f.picker->take_picked() == 9u);
    CHECK_FALSE(f.picker->take_picked().has_value());
    CHECK(two.picked);
    CHECK_FALSE(f.picker->rows()[0]->picked);
    CHECK(two.tooltip == u"Two");
}

TEST_CASE("a heading opens to show its rows and is not picked")
{
    Fixture f({ row(u"Set", 1, { row(u"Piece", 2) }), row(u"After", 3) });
    REQUIRE(f.picker->rows().size() == 3);
    BuildPickerRow& heading = *f.picker->rows()[0];
    BuildPickerRow& piece = *f.picker->rows()[1];
    BuildPickerRow& after = *f.picker->rows()[2];
    CHECK_FALSE(piece.visible);
    CHECK(after.rect().y0 == heading.rect().y0 + build_picker::kLargeRow);
    f.press(heading);
    CHECK_FALSE(f.picker->take_picked().has_value());
    CHECK(heading.open);
    CHECK(piece.visible);
    CHECK(piece.rect().x0 == heading.rect().x0 + build_picker::kIndent);
    CHECK(after.rect().y0 == heading.rect().y0 + 2 * build_picker::kLargeRow);
}

TEST_CASE("the size button makes the rows small and large again")
{
    Fixture f({ row(u"One", 1), row(u"Two", 2) });
    CHECK(f.picker->size_button().text == u"Smaller");
    f.press(f.picker->size_button());
    CHECK_FALSE(f.picker->large());
    CHECK(f.picker->size_button().text == u"Bigger");
    CHECK(f.picker->rows()[1]->rect().height() == build_picker::kSmallRow);
    f.press(f.picker->size_button());
    CHECK(f.picker->large());
}

TEST_CASE("the fold button leaves the title band, the close button closes")
{
    Fixture f({ row(u"One", 1) });
    f.press(f.picker->fold_button());
    CHECK(f.picker->folded());
    CHECK(f.picker->rect().height() == build_picker::kTitleHeight);
    f.press(f.picker->fold_button());
    CHECK(f.picker->rect().height() == build_picker::kHeight);
    f.press(f.picker->close_button());
    CHECK(f.desktop.root().find("list") == nullptr);
}
