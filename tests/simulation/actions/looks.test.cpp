// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.hpp"
#include "simulation/actions/action.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation::actions;

namespace
{
    constexpr save::model::ObjectId kRideId = 7;
    constexpr u64 kRideBuilding = 100; ///< The ride's placement, which its parts hang from
    constexpr u64 kRidePart = 101;
    constexpr u64 kEntrance = 102;
    constexpr u64 kExit = 103;
    constexpr u64 kTree = 200; ///< Scenery of no attraction

    /// A park with one ride, its building, a part hanging from it and its doors, and a tree.
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
        std::vector<save::model::PlacedObject> objects { { .id = kRideBuilding, .owner_ref = kRideBuilding }, { .id = kRidePart, .owner_ref = kRideBuilding }, { .id = kEntrance, .symbol = "OldEntrance", .owner_ref = kEntrance }, { .id = kExit, .symbol = "OldExit", .owner_ref = kExit }, { .id = kTree, .owner_ref = kTree } };
        Changed changed { };
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
                      .changed = changed };

        Park()
        {
            save::model::Ride& ride = rides.rides.emplace_back();
            ride.base = { .id = kRideId, .placement = kRideBuilding };
            ride.stations.push_back({ .entrance = kEntrance, .exit = kExit });
        }

        [[nodiscard]] const save::model::PlacedObject& object(u64 id) const
        {
            return *std::ranges::find(objects, id, &save::model::PlacedObject::id);
        }
    };
} // namespace

TEST_CASE("a prop painted takes the colour in that slot only, and is listed to be drawn again")
{
    Park p;
    REQUIRE(simulation::actions::apply(p.world, Action { PaintObject { .object = kTree, .slot = 1, .colour = 9 } }));
    CHECK(p.object(kTree).colours == std::array { 0, 9, 0 });
    CHECK(p.changed.painted_objects == std::vector { kTree });
}

TEST_CASE("painting a prop the park has not, or a slot past its three, is refused")
{
    Park p;
    CHECK(simulation::actions::apply(p.world, Action { PaintObject { .object = 999, .colour = 9 } }).error() == Refusal::Missing);
    CHECK(simulation::actions::apply(p.world, Action { PaintObject { .object = kTree, .slot = 3, .colour = 9 } }).error() == Refusal::Missing);
    CHECK(p.changed.painted_objects.empty());
}

TEST_CASE("an attraction painted paints every object of it and nothing else")
{
    Park p;
    REQUIRE(simulation::actions::apply(p.world, Action { PaintAttraction { .attraction = kRideId, .slot = 2, .colour = 4 } }));
    for (const u64 id : { kRideBuilding, kRidePart, kEntrance, kExit })
        CHECK(p.object(id).colours[2] == 4);
    CHECK(p.object(kTree).colours[2] == 0);
    CHECK(p.changed.painted_objects == std::vector { kRideBuilding, kRidePart, kEntrance, kExit });
    CHECK(simulation::actions::apply(p.world, Action { PaintAttraction { .attraction = 999, .colour = 4 } }).error() == Refusal::Missing);
}
