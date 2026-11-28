// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.hpp"
#include "simulation/actions/action.hpp"
#include "simulation/components.hpp"
#include "simulation/rides.hpp"
#include "simulation/sandbox.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation::actions;

namespace
{
    constexpr save::model::ObjectId kRideId = 7;

    /// A park with one flat ride, run by the simulation and saved under kRideId.
    struct Park
    {
        simulation::Sandbox sandbox { };
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
        World world { .sim = sandbox.world(),
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
        entt::entity ride = entt::null;

        Park()
        {
            sandbox.path({ 0, 0 }, { 5, 0 });
            ride = sandbox.flat_ride("Ride", { 5, 0 }, 200);
            sandbox.world().registry.get<simulation::Attraction>(ride).saved = kRideId;
            rides.rides.push_back({ .kind = save::model::RideKind::Flat, .base = { .id = kRideId, .name = "Ride" }, .ticket_price = { 200 } });
        }
    };
} // namespace

TEST_CASE("a ride's new ticket price is what its guests pay from then on")
{
    Park p;
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeRide { .ride = kRideId, .setting = RideSetting::TicketPrice, .value = 350 } }));
    CHECK(p.rides.rides.front().ticket_price == units::Money { 350 });
    CHECK(p.sandbox.world().registry.get<simulation::Attraction>(p.ride).price == units::Money { 350 });
}

TEST_CASE("a ride's waits turn off and on again")
{
    Park p;
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeRide { .ride = kRideId, .setting = RideSetting::MinWait, .value = 20 } }));
    CHECK(p.rides.rides.front().options.min_wait == 20);
    p.sandbox.world().registry.emplace_or_replace<simulation::Departure>(p.ride);
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeRide { .ride = kRideId, .setting = RideSetting::MaxWait, .value = 30 } }));
    CHECK(p.sandbox.world().registry.get<simulation::Departure>(p.ride).max_wait == units::Seconds { 30 });
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeRide { .ride = kRideId, .setting = RideSetting::MinWait, .value = std::nullopt } }));
    CHECK_FALSE(p.rides.rides.front().options.min_wait);
}

TEST_CASE("a ride setting or name refused changes nothing")
{
    Park p;
    SUBCASE("a ride the park does not have")
    {
        CHECK(simulation::actions::apply(p.world, Action { ChangeRide { .ride = kRideId + 1, .setting = RideSetting::TicketPrice, .value = 350 } }).error() == Refusal::Missing);
    }
    SUBCASE("a demolished ride")
    {
        p.rides.rides.front().base.demolished = true;
        CHECK(simulation::actions::apply(p.world, Action { ChangeRide { .ride = kRideId, .setting = RideSetting::TicketPrice, .value = 350 } }).error() == Refusal::Missing);
    }
    SUBCASE("a blank name")
    {
        CHECK(simulation::actions::apply(p.world, Action { RenameAttraction { .attraction = kRideId, .name = "   " } }).error() == Refusal::NothingToDo);
    }
    CHECK(p.rides.rides.front().ticket_price == units::Money { 200 });
    CHECK(p.rides.rides.front().base.name == "Ride");
    CHECK(p.sandbox.world().registry.get<simulation::Attraction>(p.ride).price == units::Money { 200 });
}

TEST_CASE("a stall synchronised passes a new price on to the stalls of its kind")
{
    Park p;
    for (save::model::ObjectId id : { 20, 21 })
        p.rides.stalls.push_back({ .base = { .id = id, .symbol = "Drinks:sta", .name = "Stall" }, .items = { { .kind_symbol = "Soda:cid", .price = { 100 } } }, .sync = true });
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStall { .stall = 20, .edit = StallEdit::ItemPrice, .item = 0, .value = 150 } }));
    CHECK(p.rides.stalls[0].items[0].price == units::Money { 150 });
    CHECK(p.rides.stalls[1].items[0].price == units::Money { 150 });
    CHECK(simulation::actions::apply(p.world, Action { ChangeStall { .stall = 20, .edit = StallEdit::ItemPrice, .item = 1, .value = 150 } }).error() == Refusal::Missing);
}

TEST_CASE("a stall left with nothing to sell closes")
{
    Park p;
    p.rides.stalls.push_back({ .base = { .id = 20, .name = "Stall", .state = save::model::AttractionState::Open }, .items = { { .kind_symbol = "Soda:cid", .price = { 100 } } } });
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStall { .stall = 20, .edit = StallEdit::RemoveItem, .item = 0 } }));
    CHECK(p.rides.stalls[0].items.empty());
    CHECK(p.rides.stalls[0].base.state == save::model::AttractionState::Closed);
}

TEST_CASE("a stall with nothing to sell is not opened, and the player is told why")
{
    Park p;
    p.rides.stalls.push_back({ .base = { .id = 20, .name = "Stall" } });
    const Outcome refused = simulation::actions::apply(p.world, Action { SetAttractionState { .attraction = 20, .state = save::model::AttractionState::Open } });
    REQUIRE_FALSE(refused);
    CHECK(refused.error() == Refusal::Forbidden);
    CHECK_FALSE(refused.error().told.empty());
    CHECK(p.rides.stalls[0].base.state == save::model::AttractionState::Closed);
    p.rides.stalls[0].items.push_back({ .kind_symbol = "Soda:cid", .price = { 100 } });
    REQUIRE(simulation::actions::apply(p.world, Action { SetAttractionState { .attraction = 20, .state = save::model::AttractionState::Open } }));
    CHECK(p.rides.stalls[0].base.state == save::model::AttractionState::Open);
}

TEST_CASE("a show ride's show keeps one trick at least")
{
    Park p;
    p.shows.push_back({ .ride = kRideId, .order = { 1, 2 } });
    REQUIRE(simulation::actions::apply(p.world, Action { EditShow { .ride = kRideId, .place = 5, .trick = 3 } }));
    CHECK(p.shows[0].order == std::vector<u32> { 1, 2, 3 });
    REQUIRE(simulation::actions::apply(p.world, Action { EditShow { .ride = kRideId, .place = 0 } }));
    REQUIRE(simulation::actions::apply(p.world, Action { EditShow { .ride = kRideId, .place = 0 } }));
    CHECK(simulation::actions::apply(p.world, Action { EditShow { .ride = kRideId, .place = 0 } }).error() == Refusal::Missing);
    CHECK(p.shows[0].order == std::vector<u32> { 3 });
}
