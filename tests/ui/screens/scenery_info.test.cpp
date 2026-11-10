// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/scenery_info.hpp"

#include "park/item_category.hpp"
#include "save/model/park.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;
using namespace rct3::ui::screens;

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

TEST_CASE("a changing room's window is an attraction's, its tabs round the ring as the game has them")
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
    using enum SceneryTab;
    CHECK(kinds == std::vector { RoomDetails, RoomFinances, RoomThoughts, RoomState, RoomMode, RoomConstruction, RoomCustomers, RoomMaintenance });
    CHECK(slots == std::vector { 0, 1, 2, 4, 6, 7, 8, 10 });
    // The construction tab acts at once, and the flag shows the complex's state.
    CHECK(std::ranges::count_if(tabs, &SceneryTabInfo::action) == 1);
    CHECK(tabs[5].action);
    CHECK(tabs[3].icon == "Attraction_General_Closed");
    CHECK(scenery_tabs({ .pool = PoolComplexFacts { .open = true } })[3].icon == "Attraction_General_Open");
    CHECK(scenery_page(RoomFinances, { .pool = PoolComplexFacts { } }, s).title == u"Pool complex");
    CHECK(scenery_page(RoomFinances, { .pool = PoolComplexFacts { .name = u"Lagoon" } }, s).title == u"Lagoon");
}

TEST_CASE("a changing room's details name the room and the complex, tell its state, and pull either down")
{
    StringTable s = table();
    s.add("SceneryItem_Hut", u"Hut room");
    s.add("AttractionStatusClosed", u"Closed");
    s.add("PeopleInPoolComplex1", u"# person inside");
    s.add("PeopleInPoolComplexX", u"# people inside");
    const auto details = [&](bool open, std::int64_t people) { return scenery_page(SceneryTab::RoomDetails, { .symbol = "Hut", .pool = PoolComplexFacts { .name = u"Lagoon", .number = 3, .open = open, .people = people } }, s); };
    const InfoPage page = details(false, 0);
    std::vector<int> ids;
    for (const InfoControl& c : page.controls)
        ids.push_back(c.id);
    CHECK(ids == std::vector { 0, kPoolNameControl, 0, kPoolDeleteRoomControl, kPoolDeleteComplexControl, kPoolSaveControl, kPoolLocateControl });
    CHECK(page.controls[0].text == u"Hut room 3");
    CHECK(page.controls[1].kind == InfoControlKind::Text);
    CHECK(page.controls[1].text == u"Lagoon");
    CHECK(page.controls[1].beside);
    CHECK(page.controls[2].label == u"Closed");
    CHECK_FALSE(page.controls[5].enabled);
    // Open, the state counts the people in the complex in the game's words for one and for many.
    CHECK(details(true, 1).controls[2].label == u"1 person inside");
    CHECK(details(true, 12).controls[2].label == u"12 people inside");
}

TEST_CASE("a changing room's finances put its ticket price over its sums, in money")
{
    StringTable s = table();
    s.add("AttractionNumberArea_PerHour", u"# an hour");
    s.add("AttractionFinancesDialogLabel_CostPerInspection", u"Inspection");
    s.add("AttractionFinancesDialogLabel_TotalProfit", u"Total");
    const InfoPage page = scenery_page(SceneryTab::RoomFinances, { .pool = PoolComplexFacts { .ticket = 150, .currency = { .before = u"$", .after = u"", .decimal = u"." }, .income = 450, .refill = 2000 } }, s);
    REQUIRE(page.controls.size() == 1);
    CHECK(page.controls[0].id == kPoolTicketControl);
    CHECK(page.controls[0].value == 150);
    CHECK(page.controls[0].shown == u"$1.50");
    CHECK(page.controls_after == 0);
    REQUIRE(page.lines.size() == 5);
    CHECK(page.lines[0].value == u"$0.00 an hour");
    CHECK(page.lines[1].label == u"Inspection");
    CHECK(page.lines[1].value == u"$20.00");
    CHECK(page.lines[4].label == u"Total");
    CHECK(page.lines[4].value == u"$4.50");
}

TEST_CASE("a changing room sets its complex's waves on bars, the machines' times only with a machine there")
{
    StringTable s = table();
    s.add("WaveHeightText", u"Height");
    s.add("WaveLengthText", u"Length");
    s.add("ManySeconds", u"# secs");
    s.add("OneMinute", u"# min");
    s.add("ManyMinutes", u"# mins");
    const auto mode = [&](bool machines, float every) { return scenery_page(SceneryTab::RoomMode, { .pool = PoolComplexFacts { .waves = { .height = 5, .frequency = 4, .on = 40, .every = every }, .machines = machines } }, s); };
    const InfoPage page = mode(true, 180);
    REQUIRE(page.controls.size() == 4);
    CHECK(page.controls[0].id == kPoolWaveHeightControl);
    CHECK(page.controls[0].kind == InfoControlKind::Bar);
    CHECK(page.controls[0].label == u"Height");
    CHECK(page.controls[0].value == 5);
    CHECK(page.controls[1].id == kPoolWaveLengthControl);
    CHECK(page.controls[1].kind == InfoControlKind::Bar);
    CHECK(page.controls[1].value == static_cast<int>(park::pools::kLeastWaveFrequency + park::pools::kMostWaveFrequency) - 4); // Longer for a lower frequency
    CHECK(page.controls[2].value == 40);
    CHECK(page.controls[2].shown == u"40 secs");
    CHECK(page.controls[3].value == 3);
    CHECK(page.controls[3].shown == u"3 mins");
    CHECK(mode(true, 60).controls[3].shown == u"1 min");
    CHECK(page.controls[3].enabled);
    CHECK_FALSE(mode(false, 180).controls[2].enabled);
    CHECK_FALSE(mode(false, 180).controls[3].enabled);
}

TEST_CASE("a changing room's customers count those it let in")
{
    StringTable s = table();
    s.add("AttractionCustomersDialogLabel_TotalCustomers", u"Total");
    const InfoPage page = scenery_page(SceneryTab::RoomCustomers, { .pool = PoolComplexFacts { .customers = 12 } }, s);
    REQUIRE(page.lines.size() == 6);
    CHECK(page.lines[4].label == u"Total");
    CHECK(page.lines[4].value == u"12");
}

TEST_CASE("a changing room's upkeep shows its reliability and water, and picks its inspections and cleaning fluid")
{
    StringTable s = table();
    s.add("PoolDirt_Label", u"Water:");
    s.add("PoolDirt_VeryDirty", u"Murky");
    s.add("Fluid_TypeLabel", u"Fluid:");
    s.add("Fluid_ZaniClenz", u"Mild");
    s.add("Fluid_MegaKleen", u"Strong");
    s.add("Fluid_ToxiScrub", u"Harsh");
    const InfoPage page = scenery_page(SceneryTab::RoomMaintenance, { .pool = PoolComplexFacts { .dirt = 3, .fluid = 1, .reliability = 0.75f, .since_inspection = 150 } }, s);
    REQUIRE(page.lines.size() == 3);
    CHECK(page.lines[0].bar == 0.75f);
    CHECK(page.controls_after == 1);
    CHECK(page.lines[1].value == u"3"); // Minutes, rounded
    CHECK(page.lines[2].label == u"Water:");
    CHECK(page.lines[2].value == u"Murky");
    REQUIRE(page.controls.size() == 3);
    CHECK(page.controls[0].id == kPoolInspectionControl);
    CHECK(page.controls[0].value == kPoolInspection);
    CHECK(page.controls[0].items.size() == 7);
    CHECK(page.controls[1].id == kPoolFluidControl);
    CHECK(page.controls[1].value == 1);
    CHECK(page.controls[1].label == u"Fluid:");
    CHECK(page.controls[1].items.size() == 3);
    CHECK(page.controls[2].id == kPoolMechanicControl);
    CHECK(page.controls[2].beside);
}

TEST_CASE("paths, rides, supports and what lies about are not props")
{
    using enum park::ItemCategory;
    for (const auto c : { Tree, SmallScenery, Bench, LitterBin, Sign, PathFeature, ParkEntrance, Fireworks, RideEvent })
        CHECK(park::is_prop(static_cast<std::uint32_t>(c)));
    for (const auto c : { SupportColumn, SupportCap, TrackPiece, PathPiece, Leftover, GuestSpawn, Ride, RideEntrance, RideExit, KeepClear, Stall, Effect })
        CHECK_FALSE(park::is_prop(static_cast<std::uint32_t>(c)));
}
