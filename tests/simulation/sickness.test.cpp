// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::simulation;

namespace
{
    std::size_t vomit(Sandbox& park)
    {
        auto& r = park.world().registry;
        return static_cast<std::size_t>(std::ranges::count_if(r.view<Litter>(), [&](entt::entity e) { return r.get<Litter>(e).vomit; }));
    }
} // namespace

TEST_CASE("nausea fades, slower while the guest is still eating")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto fasting = park.guest({ 0, 0 }, { .nausea = 0.5f });
    const auto eating = park.guest({ 0, 0 }, { .nausea = 0.5f });
    auto& r = park.world().registry;
    r.emplace<Rubbish>(eating, Rubbish { .kind = 0, .held = -1000 }); // Eating all along
    park.run_seconds(60);
    CHECK(r.get<Mood>(fasting).nausea < 0.5f);
    CHECK(r.get<Mood>(eating).nausea > r.get<Mood>(fasting).nausea);
}

TEST_CASE("guests keep off rides wilder than their stomachs take")
{
    for (const float tolerance : { 0.1f, 0.9f })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 3, 0 });
        const auto ride = park.flat_ride("Top Spin", { 3, 0 }, 0, { .intensity = 0.9f });
        const auto g = park.guest({ 0, 0 });
        park.world().registry.get<Mood>(g).tolerance = tolerance;
        park.run_seconds(300);
        CHECK((park.world().registry.get<Attraction>(ride).customers > 0) == (tolerance > 0.5f));
    }
}

TEST_CASE("guests sick from a ride are sick by its exit, the mess upsets others until a handyman sweeps it")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 0, { .capacity = 20, .cycle_seconds = 10, .nausea = 1, .exit = glm::ivec2 { 3, 0 } });
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .inspect_every = -1 });
    std::vector<entt::entity> riders;
    for (int i = 0; i < 20; ++i)
    {
        riders.push_back(park.guest({ 4, 0 }, { .nausea = 0.7f }));
        r.get<Mood>(riders.back()).tolerance = 0;
        r.get<Errand>(riders.back()) = { ErrandKind::Riding, ride, 0 };
        r.get<CycleRide>(ride).riders.push_back(riders.back());
    }
    r.get<CycleRide>(ride).phase = RidePhase::Stopping; // Done the moment it steps
    park.run(1);
    CHECK(vomit(park) > 0);
    CHECK(std::ranges::all_of(r.view<Litter>(), [&](entt::entity e) { return r.get<Litter>(e).node == *park.world().paths.find({ 3, 0, 0 }); }));
    // A guest standing in it feels queasier.
    const auto passer = park.guest({ 3, 0 });
    r.get<Errand>(passer).rest = 1000;
    r.get<Stride>(passer).progress = 1;
    const float before = r.get<Mood>(passer).nausea;
    park.run(1);
    CHECK(r.get<Mood>(passer).nausea > before);
    park.staff(StaffRole::Handyman, { 0, 0 });
    CHECK(park.run_until([&] { return vomit(park) == 0; }, 300));
}

TEST_CASE("a sick guest sits down on a bench and settles before riding again")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    park.item(PathItemKind::Bench, { 0, 0 });
    const auto g = park.guest({ 1, 0 }, { .nausea = 0.7f });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.all_of<Linger>(g); }, 60));
    CHECK(r.get<Linger>(g).at == PathItemKind::Bench);
    const float before = r.get<Mood>(g).nausea;
    park.run_seconds(20);
    CHECK(r.get<Mood>(g).nausea < before - 0.1f);
}
