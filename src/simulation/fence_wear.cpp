// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fence_wear.hpp"

#include "core/types.hpp"
#include "park/enclosures.hpp"
#include "simulation/animals.hpp"
#include "simulation/sim_world.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    namespace
    {
        constexpr const FenceWearRates& kRates = kFenceWearRates;
        /// Fences are looked at this often, not every step: the knocks are rare and the strengths are looked up by name.
        constexpr units::Seconds kWearCheck { 1 };

        FenceUpkeep& upkeep_of(SimWorld& world)
        {
            auto& ctx = world.registry.ctx();
            if (!ctx.contains<FenceUpkeep>())
                ctx.emplace<FenceUpkeep>(FenceUpkeep { .dice = dice_for(world, entt::null).state });
            return ctx.get<FenceUpkeep>();
        }
    } // namespace

    std::optional<save::model::Tile> knock_fence(save::model::EnclosureMap& map, i32 which, random::RandomState& dice)
    {
        // One of the tiles that can wear, each as likely, in a single pass over the map.
        std::optional<save::model::Tile> picked;
        u64 seen = 0;
        for (i32 row = 0; row < map.rows; ++row)
            for (i32 col = 0; col < map.cols; ++col)
                if (map.at(col, row) == which && map.wear_at(col, row) != save::model::FenceWear::Broken && rct3::park::panelled(map, { col, row }) && random::below(dice, ++seen) == 0)
                    picked = save::model::Tile { col, row };
        if (!picked)
            return std::nullopt;
        map.wear.resize(map.tiles.size(), save::model::FenceWear::Sound);
        auto& wear = map.wear[static_cast<usize>(picked->row * map.cols + picked->col)];
        wear = static_cast<save::model::FenceWear>(std::to_underlying(wear) + 1);
        return picked;
    }

    bool step_fence_wear(SimWorld& world, save::model::EnclosureMap& map, Strengths strength, units::Seconds dt)
    {
        auto& r = world.registry;
        if (map.fences.empty())
            return false;
        auto& upkeep = upkeep_of(world);
        upkeep.since_check.value += dt.value;
        if (upkeep.since_check.value < kWearCheck.value)
            return false;
        const units::Seconds passed = std::exchange(upkeep.since_check, { });
        bool changed = false;
        const auto animals = r.view<const Animal>();
        for (usize e = 0; e < map.fences.size(); ++e)
        {
            if (map.fences[e].empty())
                continue;
            const f32 fence = strength(map.fences[e]);
            bool lived_in = false;
            f32 push = 0; // Steps of strength the animals have over the fence, all told
            for (const auto [a, animal] : animals.each())
                if (animal.enclosure == static_cast<i32>(e) && animal.job != AnimalJob::Crated)
                {
                    lived_in = true;
                    push += std::max(strength(animal.species) - fence, 0.0f);
                }
            const f32 knocks = 1 / kRates.ageing.value + push / kRates.pushing.value; // A second
            if (lived_in && random::chance(upkeep.dice, knocks * passed.value) && knock_fence(map, static_cast<i32>(e), upkeep.dice))
                changed = true;
        }
        return changed;
    }

    usize knock_fences(SimWorld& world, save::model::EnclosureMap& map, usize count)
    {
        auto& dice = upkeep_of(world).dice;
        std::vector<i32> fenced;
        for (usize e = 0; e < map.fences.size(); ++e)
            if (!map.fences[e].empty())
                fenced.push_back(static_cast<i32>(e));
        usize knocked = 0;
        for (usize i = 0; i < count && !fenced.empty(); ++i)
            if (knock_fence(map, fenced[random::below(dice, fenced.size())], dice))
                ++knocked;
        return knocked;
    }

} // namespace rct3::simulation
