// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/research.hpp"

#include "simulation/tuning.hpp"

#include <cmath>
#include <cstdlib>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;
namespace units = rct3::units;

namespace
{
    constexpr float kMonth = save::model::GameClock::kUnitsPerMonth;

    struct Lab
    {
        Research research { .budget = 20000 };
        std::vector<ResearchItem> items { { .id = 1, .area = ResearchArea::ThrillRides, .cost = units::Money { 16000 } }, { .id = 2, .area = ResearchArea::Shops, .cost = units::Money { 10000 } } };
        simulation::park::ParkState park;
        save::model::Finance money;
        std::optional<std::int64_t> kept;
        random::RandomState dice = random::seeded(random::Seed { 7 });

        void pass(float units)
        {
            step_research(research, units, items, park, money, kept, dice);
        }
    };
} // namespace

TEST_CASE("research starts where the park file left it")
{
    const Research r = start_research({ .current_item = 12, .previous_item = 9, .money_spent_on_current = 668, .monthly_budget = 20000, .priorities = { 0.5f, 1.0f }, .time_since_last_spend = 6.0f });
    CHECK(r.budget == units::Money { 20000 });
    CHECK(r.spent == units::Money { 668 });
    CHECK(r.current == 12);
    CHECK(r.previous == 9);
    CHECK(r.priorities.size() == 2);
    CHECK(r.since_part == doctest::Approx(6));
    CHECK(r.invented.empty());
}

TEST_CASE("the items to research are the attractions made available and not yet invented")
{
    using rct3::park::BuildItem;
    using rct3::park::BuildKind;
    using rct3::park::BuildList;
    std::vector<BuildItem> catalogue(3);
    const auto offer = [&](std::size_t at, BuildList list, BuildKind kind, std::string symbol, std::string overlay) {
        catalogue[at].list = list;
        catalogue[at].kind = kind;
        catalogue[at].symbol = std::move(symbol);
        catalogue[at].overlay = std::move(overlay);
    };
    offer(0, BuildList::Coasters, BuildKind::TrackedRide, "Looper", "tracks/looper/looper");
    offer(1, BuildList::GentleRides, BuildKind::FlatRide, "Carousel", "style/plain/style");
    offer(2, BuildList::DrinkShops, BuildKind::Shop, "Drinks", "style/plain/style");
    save::model::RequiredContent content;
    const auto entry = [&](std::uint64_t id, std::string symbol, std::string overlay, bool available, bool invented) { content.entries.push_back({ .kind = save::model::AssetKind::Attraction, .overlay_stem = std::move(overlay), .symbol = std::move(symbol), .available = available, .invented = invented, .id = id }); };
    entry(1, "looper", "Tracks/Looper/Looper", true, false); // Case does not matter
    entry(2, "Carousel", "style/plain/style", true, true);   // Invented already
    entry(3, "Drinks", "style/plain/style", true, false);
    entry(4, "Carousel", "style/other/style", true, false); // Not in the catalogue
    entry(5, "Drinks", "style/plain/style", false, false);  // Not part of this park
    content.entries.push_back({ .kind = save::model::AssetKind::Scenery, .overlay_stem = "style/plain/style", .symbol = "Carousel", .available = true, .id = 6 });

    const std::vector<ResearchItem> items = research_items(content, catalogue, { });
    REQUIRE(items.size() == 2);
    CHECK(items[0].id == 1);
    CHECK(items[0].area == ResearchArea::Coasters);
    CHECK(items[0].cost == kTuning.research.tracked_ride_cost);
    CHECK(items[1].id == 3);
    CHECK(items[1].area == ResearchArea::Shops);
    CHECK(items[1].cost == kTuning.research.shop_cost);
}

TEST_CASE("researchable scenery and path styles join research, scenery costing by its price")
{
    using rct3::park::BuildItem;
    using rct3::park::BuildKind;
    using save::model::AssetKind;
    std::vector<BuildItem> catalogue(3);
    const auto scenery = [&](std::size_t at, std::string symbol, std::uint32_t category, std::int32_t price) {
        catalogue[at].kind = BuildKind::Scenery;
        catalogue[at].overlay = "style/plain/style";
        catalogue[at].symbol = std::move(symbol);
        catalogue[at].type = category;
        catalogue[at].price = price;
    };
    scenery(0, "Bench", 31, 1500);
    scenery(1, "Volcano", 28, 95000); // dear enough to meet the cap
    scenery(2, "Column", 12, 1000);   // never researched
    std::vector<rct3::park::path::PathStyle> paths(3);
    paths[0].name = "Tarmac";
    paths[1].name = "Plain";
    paths[1].names_category = true;
    paths[2].name = "Gold";
    paths[2].queue = true;
    save::model::RequiredContent content;
    const auto entry = [&](std::uint64_t id, AssetKind kind, std::string symbol, bool available) { content.entries.push_back({ .kind = kind, .overlay_stem = kind == AssetKind::Scenery ? "style/plain/style" : "", .symbol = std::move(symbol), .available = available, .id = id }); };
    entry(1, AssetKind::Scenery, "Bench", true);
    entry(2, AssetKind::Scenery, "Volcano", true);
    entry(3, AssetKind::Scenery, "Column", true);
    entry(4, AssetKind::PathType, "Tarmac", true);
    entry(5, AssetKind::PathType, "Plain", true);
    entry(6, AssetKind::QueueType, "Gold", true);
    entry(7, AssetKind::PathType, "Gold", true); // a queue's name, not a path's
    entry(8, AssetKind::Scenery, "Bench", false);

    const tuning::Research& t = kTuning.research;
    const std::vector<ResearchItem> items = research_items(content, catalogue, paths);
    REQUIRE(items.size() == 4);
    CHECK(items[0].id == 1);
    CHECK(items[0].area == ResearchArea::Scenery);
    CHECK(items[0].cost == units::Money { std::llround(1500 * t.scenery_cost_per_price) });
    CHECK(items[1].id == 2);
    CHECK(items[1].cost == t.scenery_cost_cap);
    CHECK(items[2].id == 4);
    CHECK(items[2].area == ResearchArea::Paths);
    CHECK(items[2].cost == t.other_cost);
    CHECK(items[3].id == 6);
    CHECK(items[3].area == ResearchArea::Paths);
}

TEST_CASE("the budget is paid in parts and booked as research spending")
{
    Lab lab;
    lab.park.cash = units::Money { 100000 };
    lab.pass(6); // Too soon for a part
    CHECK(lab.park.cash == units::Money { 100000 });
    CHECK_FALSE(lab.research.current);
    lab.pass(6); // A thirtieth of the month has gone
    CHECK(lab.park.cash == units::Money { 100000 - 667 });
    CHECK(lab.money.ledger[0].research == 667);
    CHECK(lab.research.spent == units::Money { 667 });
    CHECK(lab.research.current.has_value());
    // A whole month comes to the budget.
    for (int part = 1; part < 30; ++part)
        lab.pass(12);
    CHECK(std::abs(lab.park.cash->value - (100000 - 20000)) <= 667);
}

TEST_CASE("an item whose cost is covered is invented and the rest carries over")
{
    Lab lab;
    lab.park.cash = units::Money { 100000 };
    lab.research.current = 1;
    lab.pass(kMonth); // 20000 against a cost of 16000
    CHECK(lab.research.invented == std::vector<std::uint64_t> { 1 });
    CHECK(lab.research.previous == 1);
    CHECK(lab.research.current == 2);
    CHECK(lab.research.spent == units::Money { 4000 });
    REQUIRE(lab.items.size() == 1);
    CHECK(lab.items[0].id == 2);
    lab.pass(kMonth); // 24000 against 10000: the last item, with nothing to carry the rest to
    CHECK(lab.research.invented == std::vector<std::uint64_t> { 1, 2 });
    CHECK(lab.items.empty());
    CHECK_FALSE(lab.research.current);
    // Nothing left to invent, nothing is paid.
    const auto cash = lab.park.cash;
    lab.pass(kMonth);
    CHECK(lab.park.cash == cash);
}

TEST_CASE("a park that cannot pay a part makes no progress")
{
    Lab lab;
    lab.park.cash = units::Money { 100 };
    lab.pass(12);
    CHECK(lab.park.cash == units::Money { 100 });
    CHECK(lab.research.spent == units::Money { 0 });
    CHECK(lab.money.ledger.empty());
    CHECK(lab.park.messages.list.empty()); // Said nowhere: it happens thirty times a month
    lab.park.cash = units::Money { 1000 };
    lab.pass(12);
    CHECK(lab.research.spent == units::Money { 667 }); // The part missed is not owed
}

TEST_CASE("a park whose money never runs out researches for free")
{
    Lab lab;
    lab.research.current = 2;
    lab.pass(kMonth);
    CHECK_FALSE(lab.park.cash.has_value());
    CHECK(lab.money.ledger.empty());
    CHECK(lab.research.invented == std::vector<std::uint64_t> { 2 });
}

TEST_CASE("no budget, no research")
{
    Lab lab;
    lab.park.cash = units::Money { 100000 };
    lab.research.budget = units::Money { 0 };
    lab.pass(kMonth);
    CHECK(lab.park.cash == units::Money { 100000 });
    CHECK(lab.research.invented.empty());
    CHECK(lab.research.since_part == 0);
}

TEST_CASE("a boost counts with the next part paid")
{
    Lab lab;
    lab.park.cash = units::Money { 100000 };
    lab.research.current = 2;
    lab.research.boost = units::Money { 9500 };
    lab.pass(12); // 667 paid, 10167 counted against 10000
    CHECK(lab.park.cash == units::Money { 100000 - 667 });
    CHECK(lab.research.boost == units::Money { 0 });
    CHECK(lab.research.invented == std::vector<std::uint64_t> { 2 });
    CHECK(lab.research.spent == units::Money { 167 });
}

TEST_CASE("the next item is drawn from the areas the player cares for")
{
    SUBCASE("an area weighing nothing is passed over while another has items")
    {
        for (std::uint64_t seed = 1; seed <= 20; ++seed)
        {
            Lab lab;
            lab.dice = random::seeded(random::Seed { seed });
            lab.research.priorities.assign(kResearchAreas, 0.0f);
            lab.research.priorities[static_cast<std::size_t>(ResearchArea::Shops)] = 1.0f;
            lab.pass(12);
            CHECK(lab.research.current == 2);
        }
    }
    SUBCASE("every area weighing nothing, any item is drawn")
    {
        Lab lab;
        lab.research.priorities.assign(kResearchAreas, 0.0f);
        lab.pass(12);
        CHECK(lab.research.current.has_value());
    }
    SUBCASE("a kept item that is no longer to be invented is replaced")
    {
        Lab lab;
        lab.research.current = 99;
        lab.pass(12);
        CHECK((lab.research.current == 1 || lab.research.current == 2));
    }
    SUBCASE("both areas are drawn over many parks")
    {
        int shops = 0;
        for (std::uint64_t seed = 1; seed <= 200; ++seed)
        {
            Lab lab;
            lab.dice = random::seeded(random::Seed { seed });
            lab.research.priorities.assign(kResearchAreas, 0.5f);
            lab.research.priorities[static_cast<std::size_t>(ResearchArea::Shops)] = 1.5f;
            lab.pass(12);
            shops += lab.research.current == 2 ? 1 : 0;
        }
        CHECK(shops > 120); // Three in four, give or take
        CHECK(shops < 180);
    }
}

TEST_CASE("the progress of the current item and the months the budget takes to finish it")
{
    const std::vector<ResearchItem> items { { .id = 1, .cost = units::Money { 24000 } }, { .id = 2, .cost = units::Money { 10000 } } };
    Research r { .budget = 20000, .spent = 6000, .current = 1 };
    auto p = research_progress(r, items);
    REQUIRE(p);
    CHECK(p->id == 1);
    CHECK(p->done == doctest::Approx(0.25));
    CHECK(p->months == 1); // 180.00 left at 200.00 a month

    r.budget = units::Money { 5000 };
    CHECK(research_progress(r, items)->months == 4); // 180.00 at 50.00: three months and a bit

    SUBCASE("a boost waiting counts as spent")
    {
        r.boost = units::Money { 18000 };
        p = research_progress(r, items);
        CHECK(p->done == doctest::Approx(1));
        CHECK(p->months == 1);
    }
    SUBCASE("no budget, no estimate")
    {
        r.budget = units::Money { 0 };
        CHECK(research_progress(r, items)->months == std::nullopt);
    }
    SUBCASE("nothing under way")
    {
        r.current = 3;
        CHECK_FALSE(research_progress(r, items));
        r.current.reset();
        CHECK_FALSE(research_progress(r, items));
    }
}

TEST_CASE("an invention keeps the calendar's time it was made at")
{
    Lab lab;
    lab.park.time = 4321;
    lab.research.current = 2;
    lab.pass(kMonth);
    CHECK(lab.research.invented == std::vector<std::uint64_t> { 2 });
    CHECK(lab.research.invented_at == std::vector<float> { 4321 });
}

TEST_CASE("a focused item is worked on at once, costs more, and what it replaced is taken up again")
{
    Lab lab;
    lab.park.cash = units::Money { 100000 };
    lab.research.current = 1;
    lab.research.spent = units::Money { 4000 };
    CHECK_FALSE(focus_research(lab.research, 99, lab.items));
    CHECK(lab.research.current == 1);
    REQUIRE(focus_research(lab.research, 2, lab.items));
    CHECK(lab.research.current == 2);
    CHECK(lab.research.focus == 2);
    CHECK(lab.research.spent == units::Money { 0 });
    CHECK(research_cost(lab.items[1], lab.research.focus == lab.items[1].id) == units::Money { 15000 }); // 100.00 half as much again
    CHECK(research_cost(lab.items[0], lab.research.focus == lab.items[0].id) == units::Money { 16000 });
    lab.pass(kMonth); // 20000 against 15000
    CHECK(lab.research.invented == std::vector<std::uint64_t> { 2 });
    CHECK_FALSE(lab.research.focus);
    CHECK(lab.research.current == 1);
    CHECK(lab.research.spent == units::Money { 4000 + 5000 }); // What it had, and what was left over
    CHECK(lab.research.set_aside.empty());
}

TEST_CASE("an ended focus gives way to what it set aside, keeping what was spent on both")
{
    Lab lab;
    lab.park.cash = units::Money { 100000 };
    lab.research.current = 1;
    lab.research.spent = units::Money { 4000 };
    REQUIRE(focus_research(lab.research, 2, lab.items));
    lab.research.spent = units::Money { 3000 };
    REQUIRE(focus_research(lab.research, std::nullopt, lab.items));
    CHECK_FALSE(lab.research.focus);
    lab.pass(12); // 667
    CHECK(lab.research.current == 1);
    CHECK(lab.research.spent == units::Money { 4000 + 667 });
    REQUIRE(focus_research(lab.research, 2, lab.items));
    CHECK(lab.research.spent == units::Money { 3000 });

    SUBCASE("with nothing set aside the item goes on at its usual cost")
    {
        Lab plain;
        plain.research.current = 2;
        REQUIRE(focus_research(plain.research, 2, plain.items));
        CHECK(research_cost(plain.items[1], plain.research.focus == plain.items[1].id) == units::Money { 15000 });
        REQUIRE(focus_research(plain.research, std::nullopt, plain.items));
        CHECK(plain.research.current == 2);
        CHECK(research_cost(plain.items[1], plain.research.focus == plain.items[1].id) == units::Money { 10000 });
    }
}

TEST_CASE("the preview shows the cheapest few of each area, leaving out the item worked on")
{
    const std::vector<ResearchItem> items {
        { .id = 1, .area = ResearchArea::Shops, .cost = units::Money { 10000 } }, { .id = 2, .area = ResearchArea::Coasters, .cost = units::Money { 24000 } }, { .id = 3, .area = ResearchArea::Shops, .cost = units::Money { 6000 } }, { .id = 4, .area = ResearchArea::Shops, .cost = units::Money { 8000 } }, { .id = 5, .area = ResearchArea::Coasters, .cost = units::Money { 16000 } },
    };
    const Research r { .current = 3 };
    const std::vector<ResearchItem> next = research_preview(r, items, 2);
    std::vector<std::uint64_t> ids;
    for (const ResearchItem& i : next)
        ids.push_back(i.id);
    CHECK(ids == std::vector<std::uint64_t> { 5, 2, 4, 1 });
}
