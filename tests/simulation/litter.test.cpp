// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <vector>

using namespace rct3::simulation;

namespace
{
    std::size_t litter(Sandbox& park)
    {
        return park.world().registry.view<Litter>().size();
    }
} // namespace

TEST_CASE("a guest with no bin near carries their cup a while, then drops it on the path")
{
    Sandbox park;
    park.path({ 0, 0 }, { 0, 0 });
    park.shop("Drinks", Product::Drink, { 0, 0 }, 100);
    const auto g = park.guest({ 0, 0 }, { .thirst = 0.9f, .happiness = 1 });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.all_of<Rubbish>(g); }, 5));
    CHECK(park.run_until([&] { return litter(park) == 1; }, kConsumeSeconds + 65));
    CHECK_FALSE(r.all_of<Rubbish>(g));
}

TEST_CASE("a guest bins their rubbish, and a full bin overflows onto the path")
{
    for (const float room : { 40.0f, 0.0f })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 0, 0 });
        park.shop("Drinks", Product::Drink, { 0, 0 }, 100);
        const auto bin = park.item(PathItemKind::Bin, { 0, 0 }, room);
        const auto g = park.guest({ 0, 0 }, { .thirst = 0.9f, .happiness = 1 });
        auto& r = park.world().registry;
        CHECK(park.run_until([&] { return r.all_of<Rubbish>(g); }, 5));
        park.run_seconds(kConsumeSeconds + 1);
        CHECK_FALSE(r.all_of<Rubbish>(g));
        CHECK(r.get<Bin>(bin).contents == (room > 0 ? 1 : 0));
        CHECK(litter(park) == (room > 0 ? 0 : 1));
    }
}

TEST_CASE("a handyman sweeps up the litter and empties the bins on their way")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    park.litter({ 5, 0 }, 3).litter({ 2, 0 });
    const auto bin = park.item(PathItemKind::Bin, { 3, 0 });
    auto& r = park.world().registry;
    r.get<Bin>(bin).contents = 30;
    park.staff(StaffRole::Handyman, { 0, 0 });
    CHECK(park.run_until([&] { return litter(park) == 0 && r.get<Bin>(bin).contents == 0; }, 300));
}

TEST_CASE("an angry guest breaks what stands by them unless a guard is near, and a handyman mends it")
{
    {
        Sandbox park;
        park.path({ 0, 0 }, { 0, 0 });
        const auto bench = park.item(PathItemKind::Bench, { 0, 0 });
        park.guest({ 0, 0 }, { .happiness = 0 });
        park.staff(StaffRole::Guard, { 0, 0 });
        park.run_seconds(900);
        CHECK_FALSE(park.world().registry.get<PathItem>(bench).broken);
    }
    Sandbox park;
    park.path({ 0, 0 }, { 0, 0 });
    std::vector<entt::entity> benches;
    for (int i = 0; i < 3; ++i)
        benches.push_back(park.item(PathItemKind::Bench, { 0, 0 }));
    const auto vandal = park.guest({ 0, 0 }, { .happiness = 0 });
    auto& r = park.world().registry;
    const auto broken = [&](entt::entity b) { return r.get<PathItem>(b).broken; };
    CHECK(park.run_until([&] { return std::ranges::all_of(benches, broken); }, 3000));
    // Three broken lately: the player is told, once.
    CHECK(r.ctx().get<Vandalism>().broken == 3);
    CHECK(r.ctx().get<Vandalism>().warnings == 1);
    r.get<Mood>(vandal).happiness = 1;
    park.staff(StaffRole::Handyman, { 0, 0 });
    CHECK(park.run_until([&] { return std::ranges::none_of(benches, broken); }, 60));
}

TEST_CASE("a dirty path spoils the mood of the guests on it")
{
    Sandbox park;
    park.path({ 0, 0 }, { 0, 0 });
    park.litter({ 0, 0 }, 5);
    const auto g = park.guest({ 0, 0 }, { .happiness = 0.5f });
    park.run_seconds(60);
    CHECK(park.world().registry.get<Mood>(g).happiness < 0.5f);
}

TEST_CASE("litter counts against the rating once it has lain a while")
{
    Sandbox park;
    park.path({ 0, 0 }, { 0, 0 });
    park.litter({ 0, 0 }, 2);
    auto& r = park.world().registry;
    CHECK(old_litter(park.world()) == 0);
    r.get<Litter>(r.view<Litter>().front()).age = 1000;
    CHECK(old_litter(park.world()) == 1);
}
