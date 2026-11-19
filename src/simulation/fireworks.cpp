// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fireworks.hpp"

#include "core/types.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

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
        if (const auto* show = r.ctx().find<FireworkShow>())
        {
            const auto view = r.view<const Visitor, const Errand, Pose>(entt::exclude<Staff, Linger, Watching, Gesture, Left, Held, Dropped, RideWalk, Crossing, Swimmer, Outside, FireworkWatcher>);
            for (const entt::entity g : sorted(view))
            {
                auto [errand, pose] = view.get<const Errand, Pose>(g);
                const glm::vec2 to { show->centre.x - pose.position.x, show->centre.z - pose.position.z };
                const bool on_paths = errand.kind == ErrandKind::Wandering || errand.kind == ErrandKind::Heading || errand.kind == ErrandKind::Following || errand.kind == ErrandKind::Waiting;
                if (!on_paths || glm::length(to) > kFireworksReach.value)
                    continue;
                r.emplace<Linger>(g, Linger { .at = PathItemKind::Sight, .left = kWatchAtMost });
                r.emplace<FireworkWatcher>(g, FireworkWatcher { .dark = show->dark, .thought = show->thought });
                // Turned at once: a guest standing still is drawn between two equal headings.
                if (glm::length(to) > 0)
                    pose.heading = pose.was_heading = units::Radians { std::atan2(to.x, to.y) };
            }
            return;
        }
        const auto watchers = r.view<const FireworkWatcher>();
        for (const entt::entity g : sorted(watchers))
        {
            const auto [dark, thought] = watchers.get<const FireworkWatcher>(g);
            if (const auto* linger = r.try_get<Linger>(g); linger != nullptr && linger->at == PathItemKind::Sight && linger->bench == entt::null)
                r.remove<Linger>(g);
            if (auto* mood = r.try_get<Mood>(g); mood != nullptr && r.all_of<Stride>(g))
            {
                mood->happiness.value = clamp01(mood->happiness.value + kFireworksLift.value * (dark ? 2.0f : 1.0f));
                r.emplace_or_replace<Gesture>(g, GestureKind::Cheer, kTuning.gestures.shown.value);
                think(r, g, thought, entt::null, now);
            }
            r.remove<FireworkWatcher>(g);
        }
    }

} // namespace rct3::simulation
