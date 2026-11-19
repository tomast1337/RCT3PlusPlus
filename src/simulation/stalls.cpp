// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/stalls.hpp"

#include "core/types.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>

namespace rct3::simulation
{

    void step_stalls(SimWorld& world, std::optional<profiling::Scope>& phase)
    {
        entt::registry& r = world.registry;
        constexpr f32 dt = kTickSeconds;

        phase.emplace("Stalls");
        // Stalls and cash machines wear with time while open, the slower the more guards (by their training) the
        // park has for its guests, and break down, closing until a mechanic mends them.
        f32 guards = 0;
        for (const auto [e, staff] : r.view<const Staff>().each())
            guards += staff.role == StaffRole::Guard ? static_cast<f32>(1 + staff.training) / 4 : 0;
        const auto inside = r.view<const Visitor>(entt::exclude<Left>);
        const auto guests = static_cast<f32>(std::ranges::distance(inside.begin(), inside.end()));
        const f32 pace = 1 / (1 + guards * kTuning.wear.guard_guests / std::max(guests, kTuning.wear.guard_guests));
        for (const entt::entity e : sorted(r.view<Upkeep, Attraction>()))
        {
            auto& upkeep = r.get<Upkeep>(e);
            auto& attraction = r.get<Attraction>(e);
            if (!upkeep.break_in || upkeep.broken || !attraction.open)
                continue;
            if (upkeep.break_in->value == 0)
            {
                if (!r.all_of<Dice>(e))
                    r.emplace<Dice>(e, dice_for(world, e));
                upkeep.break_in->value = static_cast<f32>(random::between(r.get<Dice>(e).state, kTuning.wear.stall_shortest.value, kTuning.wear.stall_longest.value));
            }
            else if ((upkeep.break_in->value = std::max(upkeep.break_in->value - pace * dt, 0.0f)) == 0)
            {
                upkeep.broken = true;
                ++upkeep.breakdowns;
                attraction.open = false;
            }
        }
    }

} // namespace rct3::simulation
