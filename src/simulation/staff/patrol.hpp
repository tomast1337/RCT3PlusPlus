// SPDX-License-Identifier: GPL-3.0-or-later
//
// Staff patrols: the tiles a worker keeps to, painted by the player, and zones that several workers share.
#pragma once

#include "math/glm.hpp"
#include "simulation/components.hpp"
#include "simulation/entt.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rct3::simulation
{
    struct SimWorld;
}

namespace rct3::simulation::staff
{

    /// The tiles a staff member keeps to. They wander and work only on these, and walk back to them when
    /// they find themselves outside, after a job elsewhere. Staff without one have the whole park.
    struct Patrol
    {
        std::vector<glm::ivec2> tiles; ///< (col, row), any level
        std::uint32_t zone = 0;        ///< A shared zone: painting one on it paints all on it; 0 none

        [[nodiscard]] bool covers(const PathTileSpot& at) const
        {
            return std::ranges::contains(tiles, glm::ivec2(at.col, at.row));
        }
    };

    /// Path tiles (not queues) that no member of staff in `role` has in their patrol. None when any of them has
    /// no patrol, as they walk the whole park, or when there is no one in the role.
    [[nodiscard]] std::size_t uncovered_paths(const SimWorld& world, StaffRole role);

    /// Adds the square of tiles `across` wide round `at` to `staff`'s patrol area, or takes them away, and does
    /// the same to everyone sharing their zone. An area left with no tiles goes: the whole park again.
    void paint_patrol(entt::registry& r, entt::entity staff, glm::ivec2 at, int across, bool add);

    /// Empties `staff`'s patrol area and their zone's: the whole park again. They stay on their zone.
    void clear_patrol(entt::registry& r, entt::entity staff);

    /// Fills the box round `staff`'s patrol area, for them and everyone sharing their zone.
    void fill_patrol(entt::registry& r, entt::entity staff);

    /// Puts `staff` on shared zone `zone` (0 none). Joining a zone others are on takes its tiles.
    void join_zone(entt::registry& r, entt::entity staff, std::uint32_t zone);

} // namespace rct3::simulation::staff
