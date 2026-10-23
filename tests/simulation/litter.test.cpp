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
    r.get<Mood>(vandal).happiness.value = 1;
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
    CHECK(park.world().registry.get<Mood>(g).happiness.value < 0.5f);
}

TEST_CASE("litter counts against the rating once it has lain a while")
{
    Sandbox park;
    park.path({ 0, 0 }, { 0, 0 });
    park.litter({ 0, 0 }, 2);
    auto& r = park.world().registry;
    CHECK(old_litter(park.world()) == 0);
    r.get<Litter>(r.view<Litter>().front()).age.value = 1000;
    CHECK(old_litter(park.world()) == 1);
}

TEST_CASE("a handyman given a patrol keeps to it and sweeps it, and leaves the litter outside")
{
    Sandbox park;
    park.path({ 0, 0 }, { 9, 0 });
    park.litter({ 2, 0 }, 3).litter({ 8, 0 }, 3);
    auto& r = park.world().registry;
    const auto handyman = park.staff(StaffRole::Handyman, { 0, 0 });
    r.emplace<Patrol>(handyman, Patrol { { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } } });
    const auto& nodes = park.world().paths.nodes();
    int furthest = 0;
    park.run_until(
        [&] {
            furthest = std::max(furthest, nodes[r.get<Stride>(handyman).to].tile.col);
            return false;
        },
        600);
    CHECK(furthest <= 3);
    const auto view = r.view<const Litter>();
    CHECK(std::ranges::none_of(view, [&](entt::entity e) { return nodes[view.get<const Litter>(e).node].tile.col == 2; }));
    CHECK(std::ranges::count_if(view, [&](entt::entity e) { return nodes[view.get<const Litter>(e).node].tile.col == 8; }) == 3);
}

TEST_CASE("paths no handyman patrols are counted once every handyman has a patrol")
{
    Sandbox park;
    park.path({ 0, 0 }, { 9, 0 });
    auto& r = park.world().registry;
    const auto a = park.staff(StaffRole::Handyman, { 0, 0 });
    const auto b = park.staff(StaffRole::Handyman, { 9, 0 });
    CHECK(uncovered_paths(park.world(), StaffRole::Handyman) == 0);
    r.emplace<Patrol>(a, Patrol { { { 0, 0 }, { 1, 0 }, { 2, 0 } } });
    CHECK(uncovered_paths(park.world(), StaffRole::Handyman) == 0); // b still walks everywhere
    r.emplace<Patrol>(b, Patrol { { { 8, 0 }, { 9, 0 } } });
    CHECK(uncovered_paths(park.world(), StaffRole::Handyman) == 5);
    CHECK(uncovered_paths(park.world(), StaffRole::Mechanic) == 0);
}

TEST_CASE("a patrol is painted a brush at a time, filled to its box, and shared by those on one zone")
{
    Sandbox park;
    park.path({ 0, 0 }, { 9, 0 });
    auto& r = park.world().registry;
    const auto a = park.staff(StaffRole::Handyman, { 0, 0 });
    const auto b = park.staff(StaffRole::Handyman, { 9, 0 });
    const auto c = park.staff(StaffRole::Handyman, { 5, 0 });

    paint_patrol(r, a, { 5, 5 }, 3, true);
    CHECK(r.get<Patrol>(a).tiles.size() == 9);
    paint_patrol(r, a, { 5, 5 }, 1, false);
    CHECK(r.get<Patrol>(a).tiles.size() == 8);
    CHECK_FALSE(r.get<Patrol>(a).covers({ 5, 5, 0 }));
    fill_patrol(r, a);
    CHECK(r.get<Patrol>(a).tiles.size() == 9);
    paint_patrol(r, a, { 5, 5 }, 5, false);
    CHECK_FALSE(r.all_of<Patrol>(a)); // Nothing left: the whole park again

    // Two on one zone: painting either paints both; one more joining takes its tiles; leaving keeps their own.
    join_zone(r, a, 1);
    join_zone(r, b, 1);
    paint_patrol(r, b, { 2, 0 }, 1, true);
    CHECK(r.get<Patrol>(a).covers({ 2, 0, 0 }));
    join_zone(r, c, 1);
    CHECK(r.get<Patrol>(c).tiles == r.get<Patrol>(b).tiles);
    join_zone(r, c, 0);
    paint_patrol(r, a, { 3, 0 }, 1, true);
    CHECK(r.get<Patrol>(b).covers({ 3, 0, 0 }));
    CHECK_FALSE(r.get<Patrol>(c).covers({ 3, 0, 0 }));

    // On a zone not painted yet, a handyman still sweeps the whole park.
    const auto d = park.staff(StaffRole::Handyman, { 7, 0 });
    join_zone(r, d, 2);
    for (const auto e : { a, b, c })
        r.remove<Patrol>(e);
    park.litter({ 7, 0 }, 2);
    CHECK(park.run_until([&] { return r.view<Litter>().empty(); }, 120));
}
