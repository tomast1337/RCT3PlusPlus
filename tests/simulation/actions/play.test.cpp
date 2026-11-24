// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.hpp"
#include "simulation/actions/action.hpp"

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
        simulation::DayClock day_clock { };
        World world { .sim = sim, .park = state, .model = model, .rides = rides, .research = research, .research_items = research_items, .ledger_month = ledger_month, .day_clock = day_clock };
    };
} // namespace

TEST_CASE("a pause pressed twice plays the park on at the speed it had")
{
    Park p;
    REQUIRE(apply(p.world, Action { SetSpeed { .speed = { .speed = simulation::Speed::Fast } } }));
    REQUIRE(apply(p.world, Action { TogglePause { } }));
    CHECK(p.state.speed.paused);
    REQUIRE(apply(p.world, Action { TogglePause { } }));
    CHECK_FALSE(p.state.speed.paused);
    CHECK(p.state.speed.speed == simulation::Speed::Fast);
}

TEST_CASE("the day and night switch to the mode asked")
{
    Park p;
    REQUIRE(apply(p.world, Action { SetDayMode { .mode = simulation::DayMode::AlwaysNight } }));
    CHECK(p.day_clock.mode == simulation::DayMode::AlwaysNight);
}
