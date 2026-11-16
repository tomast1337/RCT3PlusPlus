// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/guest_edits.hpp"

#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/sim_world.hpp"

#include <algorithm>
#include <utility>

namespace rct3::simulation
{

    namespace
    {

        constexpr std::array<std::string_view, kGuestLevels> kLevelNames { "happiness", "energy", "nausea", "wet", "heat", "hunger", "thirst", "bladder", "swim", "tolerance" };

        units::Share& level_in(Mood& mood, Needs& needs, GuestLevel level)
        {
            switch (level)
            {
                case GuestLevel::Happiness:
                    return mood.happiness;
                case GuestLevel::Energy:
                    return mood.energy;
                case GuestLevel::Nausea:
                    return mood.nausea;
                case GuestLevel::Wet:
                    return mood.wet;
                case GuestLevel::Heat:
                    return mood.heat;
                case GuestLevel::Hunger:
                    return needs.hunger;
                case GuestLevel::Thirst:
                    return needs.thirst;
                case GuestLevel::Bladder:
                    return needs.bladder;
                case GuestLevel::Swim:
                    return needs.swim;
                case GuestLevel::Tolerance:
                    return mood.tolerance;
            }
            std::unreachable();
        }

    } // namespace

    std::string_view level_name(GuestLevel level)
    {
        return kLevelNames[std::to_underlying(level)];
    }

    std::optional<GuestLevel> level_named(std::string_view name)
    {
        const auto found = std::ranges::find(kLevelNames, name);
        return found == kLevelNames.end() ? std::nullopt : std::optional(static_cast<GuestLevel>(found - kLevelNames.begin()));
    }

    std::optional<units::Share> level_of(const entt::registry& r, entt::entity guest, GuestLevel level)
    {
        if (!r.valid(guest) || !r.all_of<Mood, Needs>(guest))
            return std::nullopt;
        Mood mood = r.get<const Mood>(guest);
        Needs needs = r.get<const Needs>(guest);
        return level_in(mood, needs, level);
    }

    bool set_level(entt::registry& r, entt::entity guest, GuestLevel level, units::Share to)
    {
        if (!r.valid(guest) || !r.all_of<Mood, Needs>(guest))
            return false;
        const units::Share kept { clamp01(to.value) };
        level_in(r.get<Mood>(guest), r.get<Needs>(guest), level) = kept;
        if (auto* held = r.try_get<HeldLevels>(guest); held != nullptr && held->at[std::to_underlying(level)])
            held->at[std::to_underlying(level)] = kept;
        return true;
    }

    void hold_level(entt::registry& r, entt::entity guest, GuestLevel level, bool held)
    {
        const auto now = level_of(r, guest, level);
        if (held && now)
            r.get_or_emplace<HeldLevels>(guest).at[std::to_underlying(level)] = now;
        else if (auto* levels = r.valid(guest) ? r.try_get<HeldLevels>(guest) : nullptr)
        {
            levels->at[std::to_underlying(level)].reset();
            if (std::ranges::none_of(levels->at, [](const auto& l) { return l.has_value(); }))
                r.remove<HeldLevels>(guest);
        }
    }

    bool level_held(const entt::registry& r, entt::entity guest, GuestLevel level)
    {
        const auto* held = r.valid(guest) ? r.try_get<HeldLevels>(guest) : nullptr;
        return held != nullptr && held->at[std::to_underlying(level)].has_value();
    }

    void keep_held_levels(entt::registry& r)
    {
        for (const auto [e, held, mood, needs] : r.view<const HeldLevels, Mood, Needs>().each())
            for (std::size_t k = 0; k < kGuestLevels; ++k)
                if (held.at[k])
                    level_in(mood, needs, static_cast<GuestLevel>(k)) = *held.at[k];
    }

    void make_happy_and_healthy(entt::registry& r, entt::entity guest)
    {
        for (const auto& [level, to] : { std::pair { GuestLevel::Happiness, 1.0f }, { GuestLevel::Energy, 1.0f }, { GuestLevel::Nausea, 0.0f }, { GuestLevel::Wet, 0.0f }, { GuestLevel::Heat, 0.0f }, { GuestLevel::Hunger, 0.0f }, { GuestLevel::Thirst, 0.0f }, { GuestLevel::Bladder, 0.0f } })
            (void)set_level(r, guest, level, { to });
    }

    void cure_nausea(entt::registry& r, entt::entity guest)
    {
        (void)set_level(r, guest, GuestLevel::Nausea, { 0 });
    }

    bool send_to(SimWorld& world, entt::entity guest, entt::entity target)
    {
        entt::registry& r = world.registry;
        if (!r.valid(guest) || !r.valid(target))
            return false;
        // The leader chooses where the party goes; the others follow.
        if (const auto* member = r.try_get<Member>(guest); member != nullptr && r.valid(member->party))
            guest = r.get<Party>(member->party).leader;
        auto* errand = r.try_get<Errand>(guest);
        auto* stride = r.try_get<Stride>(guest);
        if (errand == nullptr || stride == nullptr || (errand->kind != ErrandKind::Wandering && errand->kind != ErrandKind::Heading) || stride->to.value >= world.paths.nodes().size())
            return false;
        const auto* attraction = r.try_get<Attraction>(target);
        const auto* gate = r.try_get<ParkGate>(target);
        const std::optional<NodeId> node = attraction != nullptr ? attraction->entrance : gate != nullptr ? std::optional(gate->node) : std::nullopt;
        if (!node)
            return false;
        // On from the tile they are stepping to, so the step under way is not cut short.
        auto walk = world.paths.route(stride->to, *node);
        if (!walk)
            return false;
        *errand = { ErrandKind::Heading, target, 0 };
        stride->route = std::move(*walk);
        return true;
    }

    bool send_home(SimWorld& world, entt::entity guest)
    {
        const auto gates = world.registry.view<const ParkGate>();
        return !gates.empty() && send_to(world, guest, gates.front());
    }

} // namespace rct3::simulation
