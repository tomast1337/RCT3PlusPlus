// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fireworks.hpp"

#include "core/types.hpp"
#include "math/portable.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/rides.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation
{

    namespace
    {
        /// Longer than any show: a watcher's stop ends with the show, not with this.
        constexpr units::Seconds kWatchAtMost { 600 };
    } // namespace

    void watch_fireworks(SimWorld& world)
    {
        entt::registry& r = world.registry;
        const f32 now = static_cast<f32>(world.tick) * kTickSeconds;
        const auto* show = r.ctx().find<FireworkShow>();
        const auto stages = r.view<const Stage, const CycleRide>();
        const auto playing = [&](entt::entity stage) { return stages.contains(stage) && stages.get<const CycleRide>(stage).phase == RidePhase::Running; };

        // 1. Guests on the paths near a show going on stop to watch it, the display first.
        if (show != nullptr || std::ranges::any_of(stages, playing))
        {
            const std::vector<entt::entity> on_stages = sorted(stages);
            const auto view = r.view<const Visitor, const Errand, Pose>(entt::exclude<Staff, Linger, Watching, Gesture, Left, Held, Dropped, RideWalk, Crossing, Swimmer, Outside, FireworkWatcher>);
            for (const entt::entity g : sorted(view))
            {
                auto [errand, pose] = view.get<const Errand, Pose>(g);
                const bool on_paths = errand.kind == ErrandKind::Wandering || errand.kind == ErrandKind::Heading || errand.kind == ErrandKind::Following || errand.kind == ErrandKind::Waiting;
                if (!on_paths)
                    continue;
                const auto across = [&](glm::vec3 centre) { return glm::vec2 { centre.x - pose.position.x, centre.z - pose.position.z }; };
                std::optional<FireworkWatcher> watcher;
                glm::vec2 to { 0.0f };
                if (show != nullptr && glm::length(across(show->centre)) <= kFireworksReach.value)
                {
                    watcher = FireworkWatcher { .dark = show->dark, .thought = show->thought };
                    to = across(show->centre);
                }
                else
                    for (const entt::entity stage : on_stages)
                        if (playing(stage) && glm::length(across(stages.get<const Stage>(stage).centre)) <= kStageReach.value)
                        {
                            watcher = FireworkWatcher { .thought = ThoughtKind::GreatEntertainer, .stage = stage };
                            to = across(stages.get<const Stage>(stage).centre);
                            break;
                        }
                if (!watcher)
                    continue;
                r.emplace<Linger>(g, Linger { .at = PathItemKind::Sight, .left = kWatchAtMost });
                r.emplace<FireworkWatcher>(g, *watcher);
                // Turned at once: a guest standing still is drawn between two equal headings.
                if (glm::length(to) > 0)
                    pose.heading = pose.was_heading = units::Radians { math::atan2(to.x, to.y) };
            }
        }

        // 2. Watchers of a show that is over walk on, cheered.
        const auto watchers = r.view<const FireworkWatcher>();
        for (const entt::entity g : sorted(watchers))
        {
            const auto [dark, thought, stage] = watchers.get<const FireworkWatcher>(g);
            const bool on = stage == entt::null ? show != nullptr : playing(stage);
            if (on)
                continue;
            if (const auto* linger = r.try_get<Linger>(g); linger != nullptr && linger->at == PathItemKind::Sight && linger->bench == entt::null)
                r.remove<Linger>(g);
            if (auto* mood = r.try_get<Mood>(g); mood != nullptr && r.all_of<Stride>(g))
            {
                const f32 lift = stage != entt::null ? kStageLift.value : kFireworksLift.value * (dark ? 2.0f : 1.0f);
                mood->happiness.value = clamp01(mood->happiness.value + lift);
                r.emplace_or_replace<Gesture>(g, GestureKind::Cheer, kTuning.gestures.shown.value);
                think(r, g, thought, stage, now);
            }
            r.remove<FireworkWatcher>(g);
        }
    }

} // namespace rct3::simulation
