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
