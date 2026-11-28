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
        rct3::park::CarriedKinds carried { };
        std::vector<save::model::StallIndex> unjoined_stalls { };
        std::vector<rct3::park::track::TrackRules> track_rules { };
        std::vector<std::optional<rct3::park::track::TrackCourse>> courses { };
        rct3::park::pools::PoolMap pools { };
        std::optional<save::model::RideIndex> building { };
        std::vector<save::model::RideIndex> readying { };
        std::vector<save::model::RideShow> shows { };
        save::model::PeopleModel people { };
        std::vector<entt::entity> staff_walkers { };
        std::vector<rct3::park::person::Outfit> wardrobe { };
        usize costumes_given = 0;
        bool simulating = true;
        std::vector<save::model::PlacedObject> objects { };
        Changed changed { };
        save::model::TerrainGrid grid { };
        save::model::EnclosureMap enclosures { };
        World world { .sim = sim,
                      .park = state,
                      .model = model,
                      .rides = rides,
                      .research = research,
                      .research_items = research_items,
                      .ledger_month = ledger_month,
                      .day_clock = day_clock,
                      .carried = carried,
                      .unjoined_stalls = unjoined_stalls,
                      .track_rules = track_rules,
                      .courses = courses,
                      .pools = pools,
                      .building = building,
                      .readying = readying,
                      .shows = shows,
                      .people = people,
                      .staff_walkers = staff_walkers,
                      .wardrobe = wardrobe,
                      .costumes_given = costumes_given,
                      .simulating = simulating,
                      .objects = objects,
                      .changed = changed,
                      .grid = grid,
                      .enclosures = enclosures };
    };
} // namespace

TEST_CASE("a pause pressed twice plays the park on at the speed it had")
{
    Park p;
    REQUIRE(simulation::actions::apply(p.world, Action { SetSpeed { .speed = { .speed = simulation::Speed::Fast } } }));
    REQUIRE(simulation::actions::apply(p.world, Action { TogglePause { } }));
    CHECK(p.state.speed.paused);
    REQUIRE(simulation::actions::apply(p.world, Action { TogglePause { } }));
    CHECK_FALSE(p.state.speed.paused);
    CHECK(p.state.speed.speed == simulation::Speed::Fast);
}

TEST_CASE("the day and night switch to the mode asked")
{
    Park p;
    REQUIRE(simulation::actions::apply(p.world, Action { SetDayMode { .mode = simulation::DayMode::AlwaysNight } }));
    CHECK(p.day_clock.mode == simulation::DayMode::AlwaysNight);
}
