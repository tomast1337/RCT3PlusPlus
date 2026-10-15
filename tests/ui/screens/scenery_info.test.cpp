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

TEST_CASE("paths, rides, supports and what lies about are not props")
{
    using enum park::ItemCategory;
    for (const auto c : { Tree, SmallScenery, Bench, LitterBin, Sign, PathFeature, ParkEntrance, Fireworks, RideEvent })
        CHECK(park::is_prop(static_cast<std::uint32_t>(c)));
    for (const auto c : { SupportColumn, SupportCap, TrackPiece, PathPiece, Leftover, GuestSpawn, Ride, RideEntrance, RideExit, KeepClear, Stall, Effect })
        CHECK_FALSE(park::is_prop(static_cast<std::uint32_t>(c)));
}
