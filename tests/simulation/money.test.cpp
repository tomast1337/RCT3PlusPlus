// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

TEST_CASE("what guests pay at shops and rides adds up in the park's receipts")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const auto stand = park.shop("Drinks", Product::Drink, { 0, 0 }, 150);
    const auto ride = park.flat_ride("Spinner", { 6, 0 }, 200, { .capacity = 2, .cycle_seconds = 5 });
    auto& r = park.world().registry;
    park.guest({ 3, 0 }, { .thirst = 0.9f });
    const auto rider = park.guest({ 6, 0 });
    r.get<Errand>(rider) = { ErrandKind::Heading, ride, 0 };
    CHECK(park.run_until([&] { return r.get<Attraction>(stand).customers >= 1 && r.get<Attraction>(ride).customers >= 1; }, 60));
    const auto* receipts = r.ctx().find<Receipts>();
    REQUIRE(receipts != nullptr);
    CHECK(receipts->paid.value == r.get<Attraction>(stand).income.value + r.get<Attraction>(ride).income.value);
}

TEST_CASE("guests bring cash and bank around the park's averages, or the tuning's where it sets none")
{
    for (const bool averages : { true, false })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 1, 0 });
        auto& r = park.world().registry;
        if (averages)
            r.ctx().emplace<Draw>(Draw { .guest_cash = { 10000 }, .guest_bank = { 30000 } });
        const Money cash = averages ? Money { 10000 } : kTuning.arrivals.cash;
        const Money bank = averages ? Money { 30000 } : kTuning.arrivals.bank;
        rct3::random::RandomState dice = rct3::random::seeded({ 7 });
        double sum = 0;
        std::size_t count = 0;
        for (int i = 0; i < 100; ++i)
            for (const entt::entity g : admit_party(park.world(), dice, 0))
            {
                const Wallet& w = r.get<Wallet>(g);
                const double spread = kTuning.arrivals.cash_spread.value;
                CHECK(std::abs(static_cast<double>(w.cash.value - cash.value)) <= static_cast<double>(cash.value) * spread + 1);
                CHECK(std::abs(static_cast<double>(w.bank.value - bank.value)) <= static_cast<double>(bank.value) * spread + 1);
                sum += static_cast<double>(w.cash.value);
                ++count;
            }
        CHECK(sum / static_cast<double>(count) == doctest::Approx(static_cast<double>(cash.value)).epsilon(0.05));
    }
}

TEST_CASE("a cash machine gives a guest no more than their bank has left, and nothing once it is empty")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const auto machine = park.shop("Cash", Product::CashMachine, { 6, 0 }, 0);
    const auto g = park.guest({ 0, 0 }, { .cash = 100, .bank = 2000 });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.get<Wallet>(g).cash.value > 100; }, 60));
    const Wallet& w = r.get<Wallet>(g);
    CHECK(w.cash.value == 2100);
    CHECK(w.bank.value == 0);
    CHECK(w.withdrawn.value == 2000);
    // Spent down again, they have nothing more to draw.
    r.get<Wallet>(g).cash.value = 100;
    park.run_seconds(120);
    CHECK(r.get<Attraction>(machine).customers == 1);
    CHECK(r.get<Wallet>(g).cash.value == 100);
}
