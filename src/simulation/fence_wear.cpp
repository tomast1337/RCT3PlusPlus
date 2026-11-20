// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fence_wear.hpp"

#include "core/types.hpp"
#include "park/enclosures.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/staff/work.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    namespace
    {
        constexpr const FenceWearRates& kRates = kFenceWearRates;
        /// Fences are looked at this often, not every step: the knocks are rare and the strengths are looked up by name.
        constexpr units::Seconds kWearCheck { 1 };
        /// A mechanic mends a fence from any path at most this many tiles from it, as the crow flies; with none that
        /// near, from the nearest.
        constexpr i32 kMendingReach = 3;

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

    std::optional<save::model::Tile> fence_gap(const save::model::EnclosureMap& map, i32 which)
    {
        for (usize k = 0; k < map.wear.size() && k < map.tiles.size(); ++k)
            if (map.wear[k] == save::model::FenceWear::Broken && map.tiles[k] == which)
                return save::model::Tile { static_cast<i32>(k % static_cast<usize>(map.cols)), static_cast<i32>(k / static_cast<usize>(map.cols)) };
        return std::nullopt;
    }

    bool step_fence_wear(SimWorld& world, save::model::EnclosureMap& map, const AnimalGround& ground, Strengths strength, units::Seconds dt, park::ParkMessages& messages)
    {
        auto& r = world.registry;
        if (map.fences.empty())
            return false;
        bool changed = false;
        // Mechanics done at a fence leave it as new; one whose fence was put up again meanwhile has nothing to do.
        for (const auto [m, mending, worker] : r.view<staff::FenceMending, Staff>().each())
        {
            const save::model::FenceWear now = map.wear_at(mending.tile.col, mending.tile.row);
            if (now == mending.wear && (mending.step == staff::MendingStep::Walking || worker.busy.value > 0))
                continue;
            if (now == mending.wear)
            {
                map.wear[static_cast<usize>(mending.tile.row * map.cols + mending.tile.col)] = save::model::FenceWear::Sound;
                ++(now == save::model::FenceWear::Broken ? worker.fixed : worker.inspected);
                changed = true;
            }
            else
                r.get<Stride>(m).route.clear();
            r.remove<staff::FenceMending>(m);
        }
        auto& upkeep = upkeep_of(world);
        upkeep.since_check.value += dt.value;
        if (upkeep.since_check.value < kWearCheck.value)
            return changed;
        const units::Seconds passed = std::exchange(upkeep.since_check, { });
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

        // The player is told of each fence newly broken and of each mended, and reminded while any stays broken.
        std::vector<std::optional<save::model::Tile>> gaps(map.fences.size());
        for (usize k = 0; k < map.wear.size() && k < map.tiles.size(); ++k)
            if (map.wear[k] == save::model::FenceWear::Broken && map.tiles[k] && static_cast<usize>(*map.tiles[k]) < gaps.size())
                gaps[static_cast<usize>(*map.tiles[k])] = save::model::Tile { static_cast<i32>(k % static_cast<usize>(map.cols)), static_cast<i32>(k / static_cast<usize>(map.cols)) };
        const auto place = [&](save::model::Tile tile) {
            const glm::vec2 at = world.paths.origin + (glm::vec2(tile.col, tile.row) + 0.5f) * world.paths.tile_size;
            return glm::vec3(at.x, ground.height(at), at.y);
        };
        upkeep.since_told.value += passed.value;
        std::vector<bool> gapped(gaps.size());
        for (usize e = 0; e < gaps.size(); ++e)
        {
            gapped[e] = gaps[e].has_value();
            const bool was = upkeep.gapped && e < upkeep.gapped->size() && (*upkeep.gapped)[e];
            if (!upkeep.gapped || gapped[e] == was)
                continue;
            park::post(messages, { .text_key = std::string(gapped[e] ? kFenceBroken : kFenceFixed), .place = gaps[e].transform(place) });
            if (gapped[e])
                upkeep.since_told = { };
        }
        upkeep.gapped = std::move(gapped);
        if (const auto gap = std::ranges::find_if(gaps, [](const auto& g) { return g.has_value(); }); gap != gaps.end() && upkeep.since_told.value >= kRates.reminder.value)
        {
            park::post(messages, { .text_key = std::string(kFenceStillBroken), .place = place(**gap) });
            upkeep.since_told = { };
        }

        // Free mechanics are sent to worn fences, broken ones first, each fence the nearest by the paths, once the
        // rides have taken those they need.
        // shortcut: mended from the nearest path; send them by the viewing galleries into the enclosure once those exist.
        std::vector<entt::entity> free;
        for (const auto [m, worker, stride] : r.view<const Staff, const Stride>().each())
            if (worker.role == StaffRole::Mechanic && worker.ride == entt::null && !r.all_of<staff::FenceMending>(m) && stride.to.value < world.paths.nodes().size())
                free.push_back(m);
        if (free.empty() || std::ranges::all_of(map.wear, [](save::model::FenceWear w) { return w == save::model::FenceWear::Sound; }))
            return changed;
        const auto& nodes = world.paths.nodes();
        std::vector<NodeId> near;  // Path tiles in reach of the fence
        std::vector<NodeId> spots; // Those a mechanic may go to
        for (const save::model::FenceWear wear : { save::model::FenceWear::Broken, save::model::FenceWear::Damaged })
        {
            const i32 task = wear == save::model::FenceWear::Broken ? kFixRidesTask : kInspectRidesTask;
            for (usize k = 0; k < map.wear.size() && !free.empty(); ++k)
            {
                const save::model::Tile tile { static_cast<i32>(k % static_cast<usize>(map.cols)), static_cast<i32>(k / static_cast<usize>(map.cols)) };
                if (map.wear[k] != wear || std::ranges::any_of(r.view<const staff::FenceMending>(), [&](entt::entity m) { return r.get<const staff::FenceMending>(m).tile == tile; }))
                    continue;
                // Distances squared, in tiles.
                constexpr i32 kOffPaths = std::numeric_limits<i32>::max(); // A queue, or a tile taken away
                const auto off = [&](const PathNode& node) { return node.queue || node.removed ? kOffPaths : ((node.tile.col - tile.col) * (node.tile.col - tile.col)) + ((node.tile.row - tile.row) * (node.tile.row - tile.row)); };
                const i32 reach = std::max(kMendingReach * kMendingReach, std::ranges::min(nodes | std::views::transform(off)));
                near.clear();
                for (u32 n = 0; n < nodes.size(); ++n)
                    if (const i32 o = off(nodes[n]); o <= reach && o != kOffPaths)
                        near.push_back(NodeId { n });
                if (near.empty())
                    continue;
                auto who = free.end();
                std::optional<std::vector<NodeId>> best;
                for (auto m = free.begin(); m != free.end(); ++m)
                {
                    if ((r.get<const Staff>(*m).tasks & task) == 0)
                        continue;
                    const auto* patrol = r.try_get<const staff::Patrol>(*m);
                    spots.clear();
                    for (const NodeId n : near)
                        if (patrol == nullptr || patrol->tiles.empty() || patrol->covers(nodes[n.value].tile))
                            spots.push_back(n);
                    if (auto way = world.paths.route_nearest(r.get<const Stride>(*m).to, spots, Walker::Staff); way && (!best || way->walk.size() < best->size()))
                    {
                        best = std::move(way->walk);
                        who = m;
                    }
                }
                if (who == free.end())
                    continue;
                r.emplace<staff::FenceMending>(*who, staff::FenceMending { .tile = tile, .wear = wear });
                r.get<Stride>(*who).route = std::move(*best);
                free.erase(who);
            }
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
