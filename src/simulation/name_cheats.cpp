// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/name_cheats.hpp"

#include "core/types.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/tuning.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <ranges>
#include <vector>

namespace rct3::simulation
{

    std::optional<NameCheatKey> named_cheat(std::string_view typed, Cheats cheats, std::function_ref<std::string(std::string_view key)> name_of)
    {
        if (cheats == Cheats::Off)
            return std::nullopt;
        const auto found = std::ranges::find_if(kNameCheatKeys, [&](const NameCheatKey& k) { return strings::equals_ignoring_case(typed, name_of(k.key)); });
        return found == kNameCheatKeys.end() ? std::nullopt : std::optional(*found);
    }

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
            case NameCheat::Nervous:
                on.nervous = kSpell;
                return 0;
            case NameCheat::Photos:
                on.photos = kSpell;
                return 0;
            case NameCheat::Money:
            case NameCheat::GuestCamera:
            case NameCheat::Nothing:
            case NameCheat::NotYet:
                return 0;
        }
        return 0;
    }

    std::string_view set_off_message(NameCheat cheat) noexcept
    {
        switch (cheat)
        {
            case NameCheat::Unbreakable:
                return "Rct3pp_Cheat_Unbreakable";
            case NameCheat::RideAllFlat:
                return "Rct3pp_Cheat_RideAllFlat";
            case NameCheat::RideAllTracked:
                return "Rct3pp_Cheat_RideAllTracked";
            case NameCheat::BigBlasts:
                return "Rct3pp_Cheat_BigBlasts";
            case NameCheat::NotYet:
                return "Rct3pp_Cheat_NotYet";
            case NameCheat::Money:
            case NameCheat::Cheer:
            case NameCheat::Laugh:
            case NameCheat::Retch:
            case NameCheat::GuestCamera:
            case NameCheat::Hurry:
            case NameCheat::Nervous:
            case NameCheat::Photos:
            case NameCheat::Nothing:
                return { };
        }
        return { };
    }

} // namespace rct3::simulation
