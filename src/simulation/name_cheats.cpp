// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/name_cheats.hpp"

#include "core/types.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/tuning.hpp"

#include <ranges>
#include <vector>

namespace rct3::simulation
{

    usize set_off(SimWorld& world, NameCheat cheat)
    {
        entt::registry& r = world.registry;
        NameCheats& on = world.cheats;
        switch (cheat)
        {
            case NameCheat::Cheer:
            case NameCheat::Laugh:
            case NameCheat::Retch:
            {
                // Every guest on foot at once.
                const GestureKind kind = cheat == NameCheat::Cheer ? GestureKind::Cheer : cheat == NameCheat::Laugh ? GestureKind::Laugh : GestureKind::Retch;
                const auto guests = r.view<Visitor, Stride, Mood>(entt::exclude<Left>) | std::ranges::to<std::vector<entt::entity>>();
                for (const entt::entity g : guests)
                    r.emplace_or_replace<Gesture>(g, kind, kTuning.gestures.shown.value);
                return guests.size();
            }
            case NameCheat::Unbreakable:
                on.unbreakable = true;
                return 0;
            case NameCheat::RideAllFlat:
                on.ride_all_flat = true;
                return 0;
            case NameCheat::RideAllTracked:
                on.ride_all_tracked = true;
                return 0;
            case NameCheat::BigBlasts:
                on.big_blasts = true;
                return 0;
            case NameCheat::Hurry:
                on.hurry = kHurry;
                return 0;
            case NameCheat::Money:
            case NameCheat::GuestCamera:
                return 0;
        }
        return 0;
    }

} // namespace rct3::simulation
