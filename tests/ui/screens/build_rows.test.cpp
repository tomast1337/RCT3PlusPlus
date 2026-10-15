// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/build_rows.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::ui;
using rct3::park::BuildItem;
using rct3::park::BuildList;
using rct3::park::BuildOffer;

namespace
{
    BuildItem scenery(std::string symbol, BuildList list, std::string name, std::int32_t price = 0)
    {
        BuildItem i;
        i.list = list;
        i.symbol = std::move(symbol);
        i.overlay = "Style/Made/Style";
        i.name_text = std::move(name);
        i.icon = i.symbol + "Icon";
        i.price = price;
        i.width = 1;
        i.length = 2;
        return i;
    }

    const Currency kCoins { u"#", u"", u"." };
} // namespace

TEST_CASE("menu items open the list of their kind")
{
    CHECK(build_list(ToolItem::Coasters) == BuildList::Coasters);
    CHECK(build_list(ToolItem::Facilities) == BuildList::Facilities);
    CHECK(build_list(ToolItem::Plants) == BuildList::Trees);
    CHECK(build_list(ToolItem::Buildings) == BuildList::Walls);
    CHECK(build_list(ToolItem::SceneryItems) == BuildList::Props);
    CHECK_FALSE(build_list(ToolItem::Animals).has_value());
    CHECK_FALSE(build_list(ToolItem::SavedStructures).has_value());
    CHECK_FALSE(build_list(ToolItem::Staff).has_value());
}

TEST_CASE("a piece of scenery names how it sits, else its footprint")
{
    StringTable strings;
    strings.add("SceneryPlacement_Wall", u"On a wall");
    BuildItem item = scenery("Lamp", BuildList::Props, "LampName");
    CHECK(placement_text(item, strings) == u"1 x 2");
    item.placement = 3;
    CHECK(placement_text(item, strings) == u"On a wall");
    item.placement = 9;
    CHECK(placement_text(item, strings) == u"1 x 2");
}

TEST_CASE("rows are the items of one list the park offers, in name order, sets under a heading")
{
    StringTable strings;
    strings.add("ZedName", u"zed");
    strings.add("AppleName", u"Apple");
    strings.add("PeachName", u"peach");
    strings.add("SetName", u"Brick set");
    std::vector<BuildItem> items {
        scenery("Zed", BuildList::Props, "ZedName", 1250), scenery("Apple", BuildList::Props, "AppleName"), scenery("Tree", BuildList::Trees, "TreeName"), scenery("Peach", BuildList::Props, "PeachName"), scenery("Wall1", BuildList::Props, "Wall1Name"), scenery("Wall2", BuildList::Props, "Wall2Name"),
    };
    items[4].group_icon = items[5].group_icon = "SetIcon";
    items[4].group_text = items[5].group_text = "SetName";
    items[0].addon = 2;

    rct3::save::RequiredContent content;
    for (const char* symbol : { "Zed", "Apple", "Tree", "Wall1", "Wall2" })
    {
        rct3::save::AssetRequirement r;
        r.overlay_stem = "Style/Made/style";
        r.symbol = symbol;
        r.available = r.invented = true;
        content.entries.push_back(r);
    }
    const auto rows = picker_rows(BuildList::Props, items, BuildOffer(content), strings, kCoins);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].name == u"Apple");
    CHECK(rows[0].item == 1);
    CHECK(rows[0].price.empty());
    CHECK(rows[1].name == u"Brick set");
    CHECK(rows[1].icon == "SetIcon");
    REQUIRE(rows[1].children.size() == 2);
    CHECK(rows[1].children[0].item == 4);
    CHECK(rows[2].name == u"zed");
    CHECK(rows[2].price == u"#12.50");
    CHECK(rows[2].size == u"1 x 2");
    CHECK(rows[2].badge == "ObjectSelectionWindow_AOPBadge_1");

    const auto all = picker_rows(BuildList::Props, items, BuildOffer(), strings, kCoins);
    CHECK(all.size() == 4); // peach too, when the park says nothing
}

TEST_CASE("an item without a name string shows its symbol")
{
    StringTable strings;
    const std::vector<BuildItem> items { scenery("Lamp", BuildList::Props, "Missing") };
    const auto rows = picker_rows(BuildList::Props, items, BuildOffer(), strings, kCoins);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == u"Lamp");
}
