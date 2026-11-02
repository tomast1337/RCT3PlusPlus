// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::simulation;

namespace
{
    std::size_t times_found(const entt::registry& r, Finding finding, entt::entity at)
    {
        const auto* findings = r.ctx().find<Findings>();
        return findings == nullptr ? 0 : static_cast<std::size_t>(std::ranges::count(findings->found, std::pair { finding, at }));
    }

    NodeId node_at(Sandbox& park, glm::ivec2 tile)
    {
        const auto& nodes = park.world().paths.nodes();
        for (NodeId n = 0; n < nodes.size(); ++n)
            if (nodes[n].tile.col == tile.x && nodes[n].tile.row == tile.y)
                return n;
        return kNoNode;
    }
} // namespace

TEST_CASE("a stall wears with time, breaks down and shuts, and a mechanic mends it")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    auto& r = park.world().registry;
    const auto stall = park.shop("Food", Product::Food, { 4, 0 }, 100);
    r.emplace<Upkeep>(stall, Upkeep { .inspect_every = -1, .break_in = 10 });
    CHECK(park.run_until([&] { return r.get<Upkeep>(stall).broken; }, 11));
    CHECK_FALSE(r.get<Attraction>(stall).open);
    CHECK(r.get<Upkeep>(stall).breakdowns == 1);
    park.run_seconds(60); // No mechanic: it stays broken
    CHECK(r.get<Upkeep>(stall).broken);
    const auto mechanic = park.staff(StaffRole::Mechanic, { 0, 0 });
    CHECK(park.run_until([&] { return r.get<Attraction>(stall).open; }, 120));
    CHECK(r.get<Staff>(mechanic).fixed == 1);
    CHECK(r.get<Staff>(mechanic).inspected == 0);
    park.run(1);
    const float next = r.get<Upkeep>(stall).break_in.value;
    CHECK(next >= kTuning.wear.stall_shortest.value - 1);
    CHECK(next <= kTuning.wear.stall_longest.value);
}

TEST_CASE("a broken stall sells nothing")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    auto& r = park.world().registry;
    const auto stall = park.shop("Food", Product::Food, { 4, 0 }, 100);
    r.emplace<Upkeep>(stall, Upkeep { .inspect_every = -1, .break_in = 1 });
    park.run_seconds(2);
    for (int i = 0; i < 4; ++i)
        park.guest({ i, 0 }, { .hunger = 0.95f });
    park.run_seconds(120);
    CHECK(r.get<Attraction>(stall).customers == 0);
}

TEST_CASE("guards slow how fast stalls wear, the more for their training")
{
    for (const int guards : { 0, 1 })
    {
        CAPTURE(guards);
        Sandbox park;
        park.path({ 0, 0 }, { 4, 0 });
        auto& r = park.world().registry;
        const auto stall = park.shop("Food", Product::Food, { 4, 0 }, 100);
        r.emplace<Upkeep>(stall, Upkeep { .inspect_every = -1, .break_in = 100 });
        if (guards > 0)
            r.get<Staff>(park.staff(StaffRole::Guard, { 0, 0 })).training = 3;
        park.run_seconds(120);
        CHECK(r.get<Upkeep>(stall).broken == (guards == 0));
    }
}

TEST_CASE("with an inspector, a broken ride no mechanic can reach is told, again only after a while")
{
    for (const bool inspector : { false, true })
    {
        CAPTURE(inspector);
        Sandbox park;
        park.path({ 0, 0 }, { 2, 0 });
        park.path({ 6, 0 }, { 8, 0 }); // Apart from the mechanic's paths
        auto& r = park.world().registry;
        const auto ride = park.flat_ride("Twister", { 8, 0 }, 0);
        r.emplace<Upkeep>(ride, Upkeep { .broken = true, .inspect_every = -1 });
        r.get<Attraction>(ride).open = false;
        park.staff(StaffRole::Mechanic, { 0, 0 });
        if (inspector)
            r.get<Staff>(park.staff(StaffRole::Inspector, { 0, 0 })).tasks = 0;
        park.run_seconds(10);
        CHECK(times_found(r, Finding::NoMechanicCanReach, ride) == (inspector ? 1 : 0));
        CHECK(times_found(r, Finding::BadZones, ride) == 0);
        if (!inspector)
            continue;
        park.run_seconds(kTuning.inspector.quiet.value);
        CHECK(times_found(r, Finding::NoMechanicCanReach, ride) == 2);
    }
}

TEST_CASE("a mechanic keeps to their patrol, and when every patrol leaves a broken ride out the inspector says so")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    const auto ride = park.flat_ride("Twister", { 8, 0 }, 0, { .capacity = 2, .cycle_seconds = 10 });
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .broken = true, .inspect_every = -1 });
    const auto mechanic = park.staff(StaffRole::Mechanic, { 0, 0 });
    r.emplace<Patrol>(mechanic, Patrol { { { 0, 0 }, { 1, 0 }, { 2, 0 } } });
    r.get<Staff>(park.staff(StaffRole::Inspector, { 0, 0 })).tasks = 0;
    park.run_seconds(120);
    CHECK(r.get<Upkeep>(ride).broken);
    CHECK(r.get<Staff>(mechanic).ride == entt::null);
    CHECK(times_found(r, Finding::BadZones, ride) == 1);
    CHECK(times_found(r, Finding::NoMechanicCanReach, ride) == 0);
    // A mechanic with no patrol goes.
    park.staff(StaffRole::Mechanic, { 0, 0 });
    CHECK(park.run_until([&] { return !r.get<Upkeep>(ride).broken; }, 120));
}

TEST_CASE("an inspector tells of many attractions broken down at once")
{
    for (const int broken : { 1, 2 })
    {
        CAPTURE(broken);
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        auto& r = park.world().registry;
        for (int i = 0; i < 3; ++i)
        {
            const auto ride = park.flat_ride("Twister", { 2 + 2 * i, 0 }, 0);
            r.emplace<Upkeep>(ride, Upkeep { .broken = i < broken, .inspect_every = -1 });
            r.get<Attraction>(ride).open = i >= broken;
        }
        r.get<Staff>(park.staff(StaffRole::Inspector, { 0, 0 })).tasks = kFlatRidesTask;
        park.run_seconds(2 * kTuning.inspector.gap.value);
        CHECK(times_found(r, Finding::Breakdowns, entt::null) == (broken == 2 ? 1 : 0));
    }
}

TEST_CASE("an inspector tells of a worker they have no way to")
{
    Sandbox park;
    park.path({ 0, 0 }, { 2, 0 });
    park.path({ 6, 0 }, { 8, 0 });
    auto& r = park.world().registry;
    const auto worker = park.staff(StaffRole::Handyman, { 8, 0 });
    r.get<Staff>(park.staff(StaffRole::Inspector, { 0, 0 })).tasks = kStaffTask;
    park.run_seconds(2 * kTuning.inspector.gap.value);
    CHECK(times_found(r, Finding::NoPathToStaff, worker) == 1);
}

TEST_CASE("a worker whose path is taken up stands lost a while, then walks to the nearest path")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    auto& r = park.world().registry;
    const auto worker = park.staff(StaffRole::Handyman, { 4, 0 });
    const NodeId gone = node_at(park, { 4, 0 });
    park.world().paths.remove(gone);
    auto& stride = r.get<Stride>(worker);
    stride.from = stride.to = gone;
    stride.route.clear();
    park.run(1);
    CHECK(r.all_of<Lost>(worker));
    park.run_seconds(kTuning.walking.lost_wait.value / 2);
    CHECK(r.all_of<Lost>(worker));
    CHECK(park.run_until([&] { return r.all_of<Stride>(worker) && !r.all_of<Dropped>(worker) && !park.world().paths.nodes()[r.get<Stride>(worker).from].removed; }, kTuning.walking.lost_wait.value + 30));
    CHECK_FALSE(r.all_of<Lost>(worker));
}
