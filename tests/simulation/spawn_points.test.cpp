// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>
#include <functional>

using namespace rct3::simulation;

namespace
{
    /// A road from the gate at column 0 out to a spawn point at column 6.
    struct Road
    {
        Sandbox park;
        entt::entity gate = entt::null;
        entt::entity spawn = entt::null;

        explicit Road(std::int64_t fee)
        {
            park.path({ 0, 0 }, { 6, 0 });
            auto& r = park.world().registry;
            gate = r.create();
            r.emplace<ParkGate>(gate, ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
            spawn = r.create();
            r.emplace<GuestSpawn>(spawn, GuestSpawn { park.world().paths.find({ 6, 0, 0 }).value() });
            r.ctx().emplace<Draw>(Draw { .entrance_fee = { fee } });
        }

        /// The first guest to come whose `pick` holds, or the leader of their party.
        [[nodiscard]] entt::entity leader(const std::function<bool(entt::entity)>& pick)
        {
            auto& r = park.world().registry;
            for (const entt::entity e : r.view<Visitor>())
                if (pick(e))
                    return r.all_of<Member>(e) ? r.get<Party>(r.get<Member>(e).party).leader : e;
            return entt::null;
        }

        [[nodiscard]] float from(entt::entity guest, entt::entity place)
        {
            auto& r = park.world().registry;
            const NodeId node = r.all_of<ParkGate>(place) ? r.get<ParkGate>(place).node : r.get<GuestSpawn>(place).node;
            return glm::distance(r.get<Pose>(guest).position, park.world().paths.centre(node));
        }
    };
} // namespace

TEST_CASE("arriving guests appear at a spawn point and walk to the gate, paying there")
{
    Road road { 1000 };
    auto& r = road.park.world().registry;
    REQUIRE(road.park.run_until([&] { return !r.view<Visitor>().empty(); }, 20000));
    const entt::entity first = road.leader([](entt::entity) { return true; });
    CHECK(road.from(first, road.spawn) < 2.0f);
    CHECK(r.all_of<Outside>(first));
    CHECK(r.get<Wallet>(first).spent_entry.value == 0);
    REQUIRE(road.park.run_until([&] { return !r.all_of<Outside>(first); }, 120));
    CHECK(road.from(first, road.gate) < 2.0f);
    CHECK(r.get<Wallet>(first).spent_entry.value == 1000);
}

TEST_CASE("a party that cannot pay at the gate walks back to the spawn point and is gone, never counted in")
{
    Road road { 100000 };
    auto& r = road.park.world().registry;
    REQUIRE(road.park.run_until([&] { return r.ctx().contains<Turnstile>() && r.ctx().get<Turnstile>().turned_away > 0; }, 20000));
    const entt::entity first = road.leader([&](entt::entity e) { return r.all_of<Outside>(e) && r.get<Outside>(e).way == Outside::Way::TurnedAway; });
    REQUIRE(first != entt::null);
    REQUIRE(road.park.run_until([&] { return r.all_of<Left>(first); }, 120));
    CHECK(road.from(first, road.spawn) < 2.0f);
    const auto& turnstile = r.ctx().get<Turnstile>();
    CHECK(turnstile.out[0] + turnstile.out[1] + turnstile.out[2] <= turnstile.in[0] + turnstile.in[1] + turnstile.in[2]);
}

TEST_CASE("guests going home walk out of the gate and on to the nearest spawn point")
{
    Road road { 0 };
    auto& r = road.park.world().registry;
    r.get<ParkGate>(road.gate).closed = true; // Nobody else comes
    r.ctx().emplace<Daylight>(Daylight { 22.0f / 24 });
    const auto parent = road.park.guest({ 0, 0 });
    const auto child = road.park.guest({ 0, 0 });
    r.get<Visitor>(child).age = AgeGroup::Child;
    road.park.party({ parent, child });
    REQUIRE(road.park.run_until([&] { return r.all_of<Left>(parent); }, 3600));
    CHECK(r.all_of<Left>(child));
    CHECK(road.from(parent, road.spawn) < 2.0f);
}

TEST_CASE("without a way from any spawn point to the gate, guests come in at the gate")
{
    Road road { 0 };
    auto& r = road.park.world().registry;
    road.park.path({ 10, 0 }, { 10, 0 }); // A spawn point on a path that leads nowhere
    r.get<GuestSpawn>(road.spawn).node = road.park.world().paths.find({ 10, 0, 0 }).value();
    REQUIRE(road.park.run_until([&] { return !r.view<Visitor>().empty(); }, 20000));
    const entt::entity first = road.leader([](entt::entity) { return true; });
    CHECK_FALSE(r.all_of<Outside>(first));
    CHECK(road.from(first, road.gate) < 2.0f);
}
