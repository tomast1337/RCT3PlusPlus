// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/build_rows.hpp"

#include <doctest/doctest.h>
#include <format>
#include <string_view>
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
        i.icon = std::format("{}Icon", i.symbol);
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
    CHECK(build_list(ToolItem::Animals) == BuildList::Animals);
    CHECK(build_list(ToolItem::Enclosures) == BuildList::Enclosures);
    CHECK_FALSE(build_list(ToolItem::Pools).has_value());
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
    for (const char* symbol : { "Zed", "Apple", "Tree", "Wall1", "Wall2", "Peach" })
    {
        rct3::save::AssetRequirement r;
        r.overlay_stem = "Style/Made/style";
        r.symbol = symbol;
        r.available = r.invented = std::string_view(symbol) != "Peach";
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

TEST_CASE("animal rarity words")
{
    CHECK(rarity_text(0.1f) == "WildAnimalRarity_Common");
    CHECK(rarity_text(0.4f) == "WildAnimalRarity_Moderate");
    CHECK(rarity_text(0.7f) == "WildAnimalRarity_Rare");
    CHECK(rarity_text(0.9f) == "WildAnimalRarity_Endangered");
}

TEST_CASE("the enclosure list shows fences by strength, then shelters and toys, under their headings")
{
    using rct3::park::BuildKind;
    using rct3::park::EnclosurePart;
    StringTable strings;
    strings.add("EnclosureMaster_FenceTT", u"Fences");
    strings.add("EnclosureMaster_ShelterTT", u"Houses");
    strings.add("EnclosureMaster_ItemsTT", u"Toys");
    const auto item = [](std::string symbol, BuildKind kind, EnclosurePart part, float strength) {
        BuildItem i = scenery(std::move(symbol), BuildList::Enclosures, "");
        i.kind = kind;
        i.part = part;
        i.strength = strength;
        return i;
    };
    const std::vector<BuildItem> items = { item("Hoop", BuildKind::AnimalToy, EnclosurePart::Toys, 0), item("Steel", BuildKind::EnclosureFence, EnclosurePart::Fences, 3), item("Den", BuildKind::Shelter, EnclosurePart::Shelters, 0), item("Rope", BuildKind::EnclosureFence, EnclosurePart::Fences, 1) };
    const auto rows = picker_rows(BuildList::Enclosures, items, BuildOffer { }, strings, kCoins);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].name == u"Fences");
    CHECK(rows[1].name == u"Houses");
    CHECK(rows[2].name == u"Toys");
    REQUIRE(rows[0].children.size() == 2);
    CHECK(rows[0].children[0].name == u"Rope");
    CHECK(rows[0].children[1].name == u"Steel");
}

TEST_CASE("animals sit under their species, which names the fence and shelter it needs")
{
    using rct3::park::BuildKind;
    StringTable strings;
    strings.add("Wombat", u"Wombat");
    strings.add("BigWombat", u"Big wombat");
    strings.add("RopeName", u"Rope fence");
    strings.add("SteelName", u"Steel fence");
    strings.add("BurrowName", u"Burrow");
    strings.add("WildAnimalRarity", u"Rarity:");
    strings.add("WildAnimalRarity_Rare", u"Rare");
    strings.add("WildAnimal_ShelterRequired", u"Needs %SHELTERNAME%");
    std::vector<BuildItem> items;
    for (const auto& [symbol, strength] : { std::pair { "Rope", 1.0f }, std::pair { "Steel", 3.0f } })
    {
        BuildItem f = scenery(symbol, BuildList::Enclosures, std::format("{}Name", symbol));
        f.kind = BuildKind::EnclosureFence;
        f.strength = strength;
        items.push_back(f);
    }
    BuildItem burrow = scenery("burrow", BuildList::Enclosures, "BurrowName");
    burrow.kind = BuildKind::Shelter;
    items.push_back(burrow);
    BuildItem animal = scenery("wombat", BuildList::Animals, "BigWombat", 1200);
    animal.kind = BuildKind::Animal;
    animal.group_icon = "WombatIcon";
    animal.group_text = "Wombat";
    animal.strength = 2;
    animal.rarity = 0.7f;
    animal.needs = "BURROW";
    items.push_back(animal);
    const auto rows = picker_rows(BuildList::Animals, items, BuildOffer { }, strings, kCoins);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == u"Wombat (Steel fence)");
    CHECK(rows[0].size == u"Needs Burrow");
    REQUIRE(rows[0].children.size() == 1);
    CHECK(rows[0].children[0].name == u"Big wombat");
    CHECK(rows[0].children[0].size == u"Rarity: Rare");
}

TEST_CASE("items fall under the object filter of their kind of ride or category of scenery")
{
    BuildItem coaster = scenery("Woodie", BuildList::Coasters, "W");
    coaster.kind = rct3::park::BuildKind::TrackedRide;
    coaster.type = 15;
    CHECK(object_filter(coaster) == "Coaster_Wooden");
    coaster.type = 4;
    CHECK(object_filter(coaster).empty());
    BuildItem lamp = scenery("Lamp", BuildList::PathExtras, "L");
    lamp.type = 6;
    CHECK(object_filter(lamp) == "Scenery_Lamp");
    lamp.type = 0; // Scenery category 0 is trees; ride kind 0 is transport
    CHECK(object_filter(lamp) == "Scenery_Tree");
    lamp.list = BuildList::Billboards;
    CHECK(object_filter(lamp).empty());
    StringTable strings;
    lamp.list = BuildList::PathExtras;
    const std::vector items { lamp };
    CHECK(picker_rows(BuildList::PathExtras, items, BuildOffer { }, strings, kCoins).front().kind == "Scenery_Tree");
}

TEST_CASE("themes come in the install's order, once each, for the lists that filter by theme")
{
    BuildItem space = scenery("Rocket", BuildList::Props, "R");
    space.style_index = 3;
    space.style = "Space";
    space.style_title = "Sci-fi";
    BuildItem plain = scenery("Bench", BuildList::Props, "B");
    plain.style_index = 0;
    plain.style = "Vanilla";
    BuildItem none = scenery("Fence", BuildList::Props, "F");
    const std::vector items { space, plain, space, none };
    const auto themes = picker_themes(items);
    REQUIRE(themes.size() == 2);
    CHECK(themes[0].name == "Vanilla");
    CHECK(themes[1].name == "Space");
    CHECK(themes[1].title == u"Sci-fi");
    CHECK(has_theme_filters(BuildList::GentleRides));
    CHECK(has_theme_filters(BuildList::Walls));
    CHECK_FALSE(has_theme_filters(BuildList::Coasters));
    CHECK_FALSE(has_theme_filters(BuildList::Facilities));
}
