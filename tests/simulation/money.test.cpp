// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

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
