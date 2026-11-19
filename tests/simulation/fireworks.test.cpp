// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fireworks.hpp"

#include "core/types.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// How much happier a guest walking a path comes out of watching a show from `centre`, in the dark or not;
    /// none when they never stopped for it.
    std::optional<f32> lift_of_show(glm::vec3 centre, bool dark)
    {
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        const entt::entity g = park.guest({ 3, 0 });
        auto& r = park.world().registry;
        park.run(1);
        r.get<Mood>(g).happiness.value = 0.5f;
        r.ctx().emplace<FireworkShow>(FireworkShow { .centre = centre, .dark = dark });
        park.run(1);
        if (!r.all_of<FireworkWatcher, Linger>(g))
            return std::nullopt;
        // Facing the show.
        const glm::vec3 at = r.get<Pose>(g).position;
        const f32 heading = r.get<Pose>(g).heading.value;
        CHECK(std::sin(heading) * (centre.z - at.z) - std::cos(heading) * (centre.x - at.x) == doctest::Approx(0).epsilon(1e-3));
        const f32 before = r.get<Mood>(g).happiness.value;
        r.ctx().erase<FireworkShow>();
        park.run(1);
        CHECK_FALSE(r.all_of<FireworkWatcher>(g));
        CHECK_FALSE(r.all_of<Linger>(g));
        CHECK(r.get<Gesture>(g).kind == GestureKind::Cheer);
        return r.get<Mood>(g).happiness.value - before;
    }
} // namespace

TEST_CASE("guests near a firework display stop, watch it and come away happier, the more so in the dark")
{
    const auto by_day = lift_of_show({ 30, 0, 60 }, false);
    const auto at_night = lift_of_show({ 30, 0, 60 }, true);
    REQUIRE(by_day);
    REQUIRE(at_night);
    CHECK(*by_day == doctest::Approx(kFireworksLift.value).epsilon(0.1));
    CHECK(*at_night > *by_day * 1.5f);
}

TEST_CASE("guests far from a display walk on")
{
    CHECK_FALSE(lift_of_show({ kFireworksReach.value * 2, 0, 0 }, true));
}

TEST_CASE("watchers of a fountain show come away thinking of the water show")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const entt::entity g = park.guest({ 3, 0 });
    auto& r = park.world().registry;
    park.run(1);
    r.ctx().emplace<FireworkShow>(FireworkShow { .centre = { 30, 0, 60 }, .thought = ThoughtKind::WaterShowGood });
    park.run(1);
    REQUIRE(r.all_of<FireworkWatcher>(g));
    r.ctx().erase<FireworkShow>();
    park.run(1);
    const auto& recent = r.get<Thoughts>(g).recent;
    REQUIRE_FALSE(recent.empty());
    CHECK(recent.front().kind == ThoughtKind::WaterShowGood);
}
