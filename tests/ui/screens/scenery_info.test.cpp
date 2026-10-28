// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/scenery_info.hpp"

#include "park/item_category.hpp"
#include "save/model/park.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    StringTable table()
    {
        StringTable s;
        s.add("SceneryItem_StoneBin", u"Stone bin");
        s.add("Bin_AmountFull", u"Fill:");
        s.add("Bin_LastEmptied", u"Emptied:");
        s.add("Bin_NeverEmptied", u"Not yet");
        s.add("Month2", u"Spring");
        s.add("Year", u"Yr");
        s.add("Sign_Options", u"Board");
        s.add("Sign_NoEntry", u"Keep out");
        return s;
    }
} // namespace

TEST_CASE("every prop can be pulled down at once, and bins and signs get a tab of their own")
{
    const auto plain = scenery_tabs({ });
    REQUIRE(plain.size() == 1);
    CHECK(plain[0].tab == SceneryTab::Destroy);
    CHECK(plain[0].action);

    const save::BinState bin;
    const save::SignState sign;
    const auto both = scenery_tabs({ .symbol = { }, .bin = &bin, .sign = &sign });
    REQUIRE(both.size() == 3);
    CHECK(both[1].tab == SceneryTab::Bin);
    CHECK(both[2].tab == SceneryTab::Sign);
    CHECK(std::ranges::is_sorted(both, { }, &SceneryTabInfo::slot));
    CHECK(both[1].slot > 1); // The place next to pulling down is kept for colours
    CHECK_FALSE(both[1].action);
}

TEST_CASE("a prop that takes colours gets a tab with a pot for each slot it takes")
{
    SceneryFacts facts;
    facts.colour_slots = 0b101;
    facts.colours = { 3, 8, 40 };
    CHECK(scenery_paint_slots(facts) == std::vector<std::size_t> { 0, 2 });
    const auto tabs = scenery_tabs(facts);
    REQUIRE(tabs.size() == 2);
    CHECK(tabs[1].tab == SceneryTab::Colour);
    CHECK(tabs[1].slot == 1);
    // The third slot's colour is out of the range, so shown as the first.
    CHECK(tabs[1].paints == std::vector<std::size_t> { 3, 0 });
    CHECK(tabs[0].paints.empty());
}

TEST_CASE("a prop is named after its asset, whatever the case of the name")
{
    const StringTable s = table();
    CHECK(scenery_name({ .symbol = "stonebin", .bin = nullptr, .sign = nullptr }, s) == u"Stone bin");
    CHECK(scenery_name({ .symbol = "Unknown", .bin = nullptr, .sign = nullptr }, s).empty());
    const InfoPage page = scenery_page(std::nullopt, { .symbol = "StoneBin", .bin = nullptr, .sign = nullptr }, s);
    CHECK(page.title == u"Stone bin");
    CHECK(page.lines.empty());
}

TEST_CASE("a bin tells how full it is and when it was emptied")
{
    const StringTable s = table();
    save::BinState bin;
    bin.capacity = 2;
    bin.contents = 0.5f;
    const SceneryFacts facts { .symbol = "StoneBin", .bin = &bin, .sign = nullptr };
    InfoPage page = scenery_page(SceneryTab::Bin, facts, s);
    REQUIRE(page.lines.size() == 2);
    CHECK(page.lines[0].label == u"Fill:");
    CHECK(page.lines[0].value == u"25%");
    CHECK(page.lines[1].value == u"Not yet");

    // Two months and a little into the first year.
    bin.last_emptied = 2 * save::GameClock::kUnitsPerMonth + 10;
    page = scenery_page(SceneryTab::Bin, facts, s);
    CHECK(page.lines[1].value == u"Spring, Yr 1");

    // The date is counted from where the park's calendar starts: the same time is a year and a month earlier.
    const SceneryFacts later { .symbol = "StoneBin", .bin = &bin, .sign = nullptr, .zero_time = -(save::GameClock::kUnitsPerYear + save::GameClock::kUnitsPerMonth) };
    CHECK(scenery_page(SceneryTab::Bin, later, s).lines[1].value != u"Spring, Yr 1");
    CHECK(scenery_page(SceneryTab::Bin, later, s).lines[1].value.ends_with(u"Yr 2"));
}

TEST_CASE("a sign shows its words, or that it forbids entry")
{
    const StringTable s = table();
    save::SignState sign;
    sign.text = "Hello";
    const SceneryFacts facts { .symbol = { }, .bin = nullptr, .sign = &sign };
    InfoPage page = scenery_page(SceneryTab::Sign, facts, s);
    CHECK(page.heading == u"Board");
    REQUIRE(page.lines.size() == 1);
    CHECK(page.lines[0].label == u"Hello");
    sign.kind = 1;
    CHECK(scenery_page(SceneryTab::Sign, facts, s).lines[0].label == u"Keep out");
}

TEST_CASE("a changing room's window is an attraction's, about its pool complex")
{
    StringTable s = table();
    s.add("PoolComplexDefaultName", u"Pool complex");
    const auto tabs = scenery_tabs({ .pool = PoolComplexFacts { } });
    std::vector<SceneryTab> kinds;
    std::vector<int> slots;
    for (const SceneryTabInfo& t : tabs)
    {
        kinds.push_back(t.tab);
        slots.push_back(t.slot);
    }
    CHECK(kinds == std::vector { SceneryTab::RoomDetails, SceneryTab::RoomFinances, SceneryTab::RoomMode, SceneryTab::RoomCustomers, SceneryTab::RoomMaintenance });
    CHECK(slots == std::vector { 0, 1, 6, 8, 10 });
    CHECK(scenery_page(SceneryTab::RoomFinances, { .pool = PoolComplexFacts { } }, s).title == u"Pool complex");
    CHECK(scenery_page(SceneryTab::RoomFinances, { .pool = PoolComplexFacts { .name = u"Lagoon" } }, s).title == u"Lagoon");
}

TEST_CASE("a changing room's details name the complex, pull down the room or the complex, and find it")
{
    const InfoPage page = scenery_page(SceneryTab::RoomDetails, { .pool = PoolComplexFacts { .name = u"Lagoon" } }, table());
    std::vector<int> ids;
    for (const InfoControl& c : page.controls)
        ids.push_back(c.id);
    CHECK(ids == std::vector { kPoolNameControl, kPoolDeleteRoomControl, kPoolDeleteComplexControl, kPoolLocateControl });
    CHECK(page.controls[0].kind == InfoControlKind::Text);
    CHECK(page.controls[0].text == u"Lagoon");
}

TEST_CASE("a changing room opens and closes its pool complex and sets its ticket price")
{
    StringTable s = table();
    s.add("AttractionStatusOpen", u"Open");
    s.add("AttractionStatusClosed", u"Closed");
    const SceneryFacts facts { .pool = PoolComplexFacts { .ticket = 150, .open = true } };
    const InfoPage mode = scenery_page(SceneryTab::RoomMode, facts, s);
    REQUIRE(mode.controls.size() == 1);
    CHECK(mode.controls[0].id == kPoolStateControl);
    CHECK(mode.controls[0].value == 0);
    CHECK(mode.controls[0].label == u"Open");
    const InfoPage finances = scenery_page(SceneryTab::RoomFinances, facts, s);
    REQUIRE(finances.controls.size() == 1);
    CHECK(finances.controls[0].id == kPoolTicketControl);
    CHECK(finances.controls[0].value == 150);
    CHECK(finances.controls[0].decimals == 2);
}

TEST_CASE("a changing room counts the people in its complex in the game's words")
{
    StringTable s = table();
    s.add("PeopleInPoolComplex1", u"# person inside");
    s.add("PeopleInPoolComplexX", u"# people inside");
    CHECK(scenery_page(SceneryTab::RoomCustomers, { .pool = PoolComplexFacts { .people = 1 } }, s).lines.at(0).label == u"1 person inside");
    CHECK(scenery_page(SceneryTab::RoomCustomers, { .pool = PoolComplexFacts { .people = 12 } }, s).lines.at(0).label == u"12 people inside");
}

TEST_CASE("a changing room's upkeep shows its water's dirt and picks its cleaning fluid")
{
    StringTable s = table();
    s.add("PoolDirt_Label", u"Water:");
    s.add("PoolDirt_VeryDirty", u"Murky");
    s.add("Fluid_TypeLabel", u"Fluid:");
    s.add("Fluid_ZaniClenz", u"Mild");
    s.add("Fluid_MegaKleen", u"Strong");
    s.add("Fluid_ToxiScrub", u"Harsh");
    const InfoPage page = scenery_page(SceneryTab::RoomMaintenance, { .pool = PoolComplexFacts { .dirt = 3, .fluid = 1 } }, s);
    REQUIRE(page.lines.size() == 1);
    CHECK(page.lines[0].label == u"Water:");
    CHECK(page.lines[0].value == u"Murky");
    REQUIRE(page.controls.size() == 2);
    CHECK(page.controls[0].id == kPoolFluidControl);
    CHECK(page.controls[0].value == 1);
    CHECK(page.controls[0].label == u"Fluid:");
    CHECK(page.controls[0].items.size() == 3);
    CHECK(page.controls[1].id == kPoolMechanicControl);
    CHECK(page.controls[1].enabled);
}

TEST_CASE("paths, rides, supports and what lies about are not props")
{
    using enum park::ItemCategory;
    for (const auto c : { Tree, SmallScenery, Bench, LitterBin, Sign, PathFeature, ParkEntrance, Fireworks, RideEvent })
        CHECK(park::is_prop(static_cast<std::uint32_t>(c)));
    for (const auto c : { SupportColumn, SupportCap, TrackPiece, PathPiece, Leftover, GuestSpawn, Ride, RideEntrance, RideExit, KeepClear, Stall, Effect })
        CHECK_FALSE(park::is_prop(static_cast<std::uint32_t>(c)));
}
