// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/staff/patrol.hpp"

#include "simulation/sim_world.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace rct3::simulation::staff
{

    namespace
    {
        /// `staff` and everyone sharing their zone.
        std::vector<entt::entity> zone_of(const entt::registry& r, entt::entity staff)
        {
            const auto* patrol = r.try_get<Patrol>(staff);
            if (patrol == nullptr || patrol->zone == 0)
                return { staff };
            std::vector<entt::entity> all;
            for (const auto [e, p] : r.view<const Patrol>().each())
                if (p.zone == patrol->zone)
                    all.push_back(e);
            return all;
        }

        /// Sets the tiles of `staff`'s zone; with none left, the zone keeps them only as members.
        void set_tiles(entt::registry& r, entt::entity staff, const std::vector<glm::ivec2>& tiles)
        {
            for (const entt::entity e : zone_of(r, staff))
            {
                if (tiles.empty() && r.get_or_emplace<Patrol>(e).zone == 0)
                    r.remove<Patrol>(e);
                else
                    r.get_or_emplace<Patrol>(e).tiles = tiles;
            }
        }
    } // namespace

    void paint_patrol(entt::registry& r, entt::entity staff, glm::ivec2 at, int across, bool add)
    {
        const auto* patrol = r.try_get<Patrol>(staff);
        std::vector<glm::ivec2> tiles = patrol != nullptr ? patrol->tiles : std::vector<glm::ivec2> { };
        for (int dc = -across / 2; dc <= across / 2; ++dc)
            for (int dr = -across / 2; dr <= across / 2; ++dr)
            {
                const glm::ivec2 t = at + glm::ivec2(dc, dr);
                const auto in = std::ranges::find(tiles, t);
                if (add && in == tiles.end())
                    tiles.push_back(t);
                else if (!add && in != tiles.end())
                    tiles.erase(in);
            }
        set_tiles(r, staff, tiles);
    }

    void clear_patrol(entt::registry& r, entt::entity staff)
    {
        set_tiles(r, staff, { });
    }

    void fill_patrol(entt::registry& r, entt::entity staff)
    {
        const auto* patrol = r.try_get<Patrol>(staff);
        if (patrol == nullptr || patrol->tiles.empty())
            return;
        glm::ivec2 low = patrol->tiles.front();
        glm::ivec2 high = low;
        for (const glm::ivec2 t : patrol->tiles)
        {
            low = glm::min(low, t);
            high = glm::max(high, t);
        }
        std::vector<glm::ivec2> tiles;
        for (int col = low.x; col <= high.x; ++col)
            for (int row = low.y; row <= high.y; ++row)
                tiles.emplace_back(col, row);
        set_tiles(r, staff, tiles);
    }

    void join_zone(entt::registry& r, entt::entity staff, std::uint32_t zone)
    {
        if (zone == 0)
        {
            if (auto* patrol = r.try_get<Patrol>(staff))
            {
                patrol->zone = 0;
                if (patrol->tiles.empty())
                    r.remove<Patrol>(staff);
            }
            return;
        }
        std::optional<std::vector<glm::ivec2>> tiles;
        for (const auto [e, p] : r.view<const Patrol>().each())
            if (e != staff && p.zone == zone)
                tiles = p.tiles;
        auto& patrol = r.get_or_emplace<Patrol>(staff);
        patrol.zone = zone;
        if (tiles)
            patrol.tiles = std::move(*tiles);
    }

    std::size_t uncovered_paths(const SimWorld& world, StaffRole role)
    {
        std::vector<const Patrol*> areas;
        for (const auto [e, staff] : world.registry.view<const Staff>().each())
        {
            if (staff.role != role)
                continue;
            const auto* patrol = world.registry.try_get<Patrol>(e);
            if (patrol == nullptr || patrol->tiles.empty())
                return 0;
            areas.push_back(patrol);
        }
        if (areas.empty())
            return 0;
        return static_cast<std::size_t>(std::ranges::count_if(world.paths.nodes(), [&](const PathNode& n) { return !n.queue && !n.removed && std::ranges::none_of(areas, [&](const Patrol* a) { return a->covers(n.tile); }); }));
    }

} // namespace rct3::simulation::staff
