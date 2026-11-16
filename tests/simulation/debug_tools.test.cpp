// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/debug_tools.hpp"

#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/sandbox.hpp"

#include <array>
#include <doctest/doctest.h>
#include <ranges>

using namespace rct3;
using namespace rct3::simulation;

TEST_CASE("debug tools: finishing research invents everything left at once")
{
    Research research { .spent = units::Money { 500 }, .current = 2, .focus = 2, .unfocused = 1, .set_aside = { { .id = 1, .spent = units::Money { 40 } } } };
    std::vector<ResearchItem> items { { .id = 1, .cost = units::Money { 100 } }, { .id = 2, .cost = units::Money { 200 } } };
    invent_everything(research, items, 42);
    CHECK(items.empty());
    CHECK(research.invented == std::vector<std::uint64_t> { 1, 2 });
    CHECK(research.invented_at == std::vector<float> { 42, 42 });
    CHECK(research.previous == 2);
    CHECK_FALSE(research.current);
    CHECK_FALSE(research.focus);
    CHECK(research.set_aside.empty());
    CHECK(research.spent == units::Money { });
}
