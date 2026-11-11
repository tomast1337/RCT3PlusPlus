// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/research.hpp"

#include "simulation/tuning.hpp"

#include <cstdlib>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    constexpr float kMonth = save::model::GameClock::kUnitsPerMonth;

    struct Lab
    {
        Research research { .budget = 20000 };
        std::vector<ResearchItem> items { { .id = 1, .area = ResearchArea::ThrillRides, .cost = 16000 }, { .id = 2, .area = ResearchArea::Shops, .cost = 10000 } };
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
    CHECK(r.budget == 20000);
    CHECK(r.spent == 668);
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

    const std::vector<ResearchItem> items = research_items(content, catalogue);
    REQUIRE(items.size() == 2);
    CHECK(items[0].id == 1);
    CHECK(items[0].area == ResearchArea::Coasters);
    CHECK(items[0].cost == kTuning.research.tracked_ride_cost.value);
    CHECK(items[1].id == 3);
    CHECK(items[1].area == ResearchArea::Shops);
    CHECK(items[1].cost == kTuning.research.shop_cost.value);
}

TEST_CASE("the budget is paid in parts and booked as research spending")
{
    Lab lab;
    lab.park.cash = 100000;
    lab.pass(6); // Too soon for a part
    CHECK(lab.park.cash == 100000);
    CHECK_FALSE(lab.research.current);
    lab.pass(6); // A thirtieth of the month has gone
    CHECK(lab.park.cash == 100000 - 667);
    CHECK(lab.money.ledger[0].research == 667);
    CHECK(lab.research.spent == 667);
    CHECK(lab.research.current.has_value());
    // A whole month comes to the budget.
    for (int part = 1; part < 30; ++part)
        lab.pass(12);
    CHECK(std::abs(*lab.park.cash - (100000 - 20000)) <= 667);
}

TEST_CASE("an item whose cost is covered is invented and the rest carries over")
{
    Lab lab;
    lab.park.cash = 100000;
    lab.research.current = 1;
    lab.pass(kMonth); // 20000 against a cost of 16000
    CHECK(lab.research.invented == std::vector<std::uint64_t> { 1 });
    CHECK(lab.research.previous == 1);
    CHECK(lab.research.current == 2);
    CHECK(lab.research.spent == 4000);
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
    lab.park.cash = 100;
    lab.pass(12);
    CHECK(lab.park.cash == 100);
    CHECK(lab.research.spent == 0);
    CHECK(lab.money.ledger[0].research == 0);
    CHECK(lab.park.messages.list.empty()); // Said nowhere: it happens thirty times a month
    lab.park.cash = 1000;
    lab.pass(12);
    CHECK(lab.research.spent == 667); // The part missed is not owed
}

TEST_CASE("a park whose money never runs out researches for free")
{
    Lab lab;
    lab.research.current = 2;
    lab.pass(kMonth);
    CHECK_FALSE(lab.park.cash.has_value());
    CHECK(lab.money.ledger[0].research == 0);
    CHECK(lab.research.invented == std::vector<std::uint64_t> { 2 });
}

TEST_CASE("no budget, no research")
{
    Lab lab;
    lab.park.cash = 100000;
    lab.research.budget = 0;
    lab.pass(kMonth);
    CHECK(lab.park.cash == 100000);
    CHECK(lab.research.invented.empty());
    CHECK(lab.research.since_part == 0);
}

TEST_CASE("a boost counts with the next part paid")
{
    Lab lab;
    lab.park.cash = 100000;
    lab.research.current = 2;
    lab.research.boost = 9500;
    lab.pass(12); // 667 paid, 10167 counted against 10000
    CHECK(lab.park.cash == 100000 - 667);
    CHECK(lab.research.boost == 0);
    CHECK(lab.research.invented == std::vector<std::uint64_t> { 2 });
    CHECK(lab.research.spent == 167);
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
