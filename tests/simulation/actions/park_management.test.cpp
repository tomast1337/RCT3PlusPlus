// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.hpp"
#include "simulation/actions/action.hpp"
#include "simulation/components.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation::actions;

namespace
{
    struct Park
    {
        simulation::SimWorld sim { };
        simulation::park::ParkState state { };
        save::model::ParkModel model { };
        save::model::RideModel rides { };
        simulation::Research research { };
        std::optional<std::vector<simulation::ResearchItem>> research_items { };
        std::optional<i64> ledger_month { };
        World world { .sim = sim, .park = state, .model = model, .rides = rides, .research = research, .research_items = research_items, .ledger_month = ledger_month };
    };
} // namespace

TEST_CASE("closing the park shuts its gates, and opening it lets guests in again")
{
    Park p;
    const entt::entity gate = p.sim.registry.create();
    p.sim.registry.emplace<simulation::ParkGate>(gate);
    REQUIRE(apply(p.world, Action { OpenPark { .open = false } }));
    CHECK(p.sim.registry.get<simulation::ParkGate>(gate).closed);
    CHECK((p.model.park.flags.raw & std::to_underlying(save::model::ParkFlag::Open)) == 0);
    REQUIRE(apply(p.world, Action { OpenPark { .open = true } }));
    CHECK_FALSE(p.sim.registry.get<simulation::ParkGate>(gate).closed);
    CHECK((p.model.park.flags.raw & std::to_underlying(save::model::ParkFlag::Open)) != 0);
}

TEST_CASE("a research area's priority is set, the areas before it weighing the most")
{
    Park p;
    REQUIRE(apply(p.world, Action { SetResearchPriority { .area = 2, .weight = 0.25f } }));
    CHECK(p.research.priorities == std::vector { 1.0f, 1.0f, 0.25f });
    CHECK(apply(p.world, Action { SetResearchPriority { .area = simulation::kResearchAreas, .weight = 0.5f } }).error() == Refusal::Missing);
    CHECK(p.research.priorities.size() == 3);
}

TEST_CASE("research is not focused before the park knows what it can research")
{
    Park p;
    CHECK(apply(p.world, Action { FocusResearch { .item = 4 } }).error() == Refusal::Missing);
    CHECK_FALSE(p.research.focus);
}
