// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.hpp"
#include "simulation/actions/action.hpp"
#include "simulation/components.hpp"
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
        World world { .sim = sandbox.world(), .park = state, .model = model, .rides = rides };
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
    REQUIRE(apply(p.world, Action { ChangeRide { .ride = kRideId, .setting = RideSetting::TicketPrice, .value = 350 } }));
    CHECK(p.rides.rides.front().ticket_price == units::Money { 350 });
    CHECK(p.sandbox.world().registry.get<simulation::Attraction>(p.ride).price == units::Money { 350 });
}

TEST_CASE("a ride's waits turn off and on again")
{
    Park p;
    REQUIRE(apply(p.world, Action { ChangeRide { .ride = kRideId, .setting = RideSetting::MinWait, .value = 20 } }));
    CHECK(p.rides.rides.front().options.min_wait == 20);
    REQUIRE(apply(p.world, Action { ChangeRide { .ride = kRideId, .setting = RideSetting::MinWait, .value = std::nullopt } }));
    CHECK_FALSE(p.rides.rides.front().options.min_wait);
}

TEST_CASE("a ride setting or name refused changes nothing")
{
    Park p;
    SUBCASE("a ride the park does not have")
    {
        CHECK(apply(p.world, Action { ChangeRide { .ride = kRideId + 1, .setting = RideSetting::TicketPrice, .value = 350 } }).error() == Refusal::Missing);
    }
    SUBCASE("a demolished ride")
    {
        p.rides.rides.front().base.demolished = true;
        CHECK(apply(p.world, Action { ChangeRide { .ride = kRideId, .setting = RideSetting::TicketPrice, .value = 350 } }).error() == Refusal::Missing);
    }
    SUBCASE("a blank name")
    {
        CHECK(apply(p.world, Action { RenameAttraction { .attraction = kRideId, .name = "   " } }).error() == Refusal::NothingToDo);
    }
    CHECK(p.rides.rides.front().ticket_price == units::Money { 200 });
    CHECK(p.rides.rides.front().base.name == "Ride");
    CHECK(p.sandbox.world().registry.get<simulation::Attraction>(p.ride).price == units::Money { 200 });
}
