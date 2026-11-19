// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/holds.hpp"

#include "core/types.hpp"
#include "park/boundary_fence.hpp"
#include "park/ground/paths.hpp"
#include "park/ground/queues.hpp"
#include "park/item_category.hpp"
#include "park/object/placement.hpp"
#include "park/path/furniture.hpp"
#include "park/queue_fences.hpp"
#include "park/support/columns.hpp"
#include "park/water/fill.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rct3::park::ground
{

    namespace
    {
        constexpr f32 kSame = 0.001f; ///< Heights closer than this have not moved

        u32 key_of(i32 col, i32 row)
        {
            return static_cast<u32>(row) << 16 | static_cast<u16>(col);
        }

        std::vector<save::model::Tile> under(const save::model::PlacedObject& o, const object::ObjectModel& m)
        {
            support::SupportFootprint shape;
            shape.x_squares = m.squares[0];
            shape.z_squares = m.squares[1];
            return support::footprint_tiles(o, shape);
        }
    } // namespace

    GroundHolds ground_holds(std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid)
    {
        GroundHolds out;
        std::vector<save::model::PlacedObject> raised;
        for (usize i = 0; i < objects.size(); ++i)
        {
            const save::model::PlacedObject& o = objects[i];
            if (o.hidden || o.fixture)
                continue;
            const auto model = models.find(object::model_ref(o));
            if (model == models.end())
                continue;
            const object::ObjectModel& m = model->second;
            if (m.holds_ground)
            {
                for (const auto [c, r] : under(o, m))
                    out.held.insert(key_of(c, r));
            }
            else if (object::on_ground(o) && is_prop(m.category) && !path::on_paths(static_cast<object::Positioning>(m.positioning)))
                out.followers.push_back(i);
            else if (!m.solid.empty())
                raised.push_back(o);
        }
        out.raised = path::solid_tiles(raised, models, grid);
        for (const save::model::PathTile& p : paths)
        {
            if (p.elevation && p.elevation->underground)
                continue;
            const u32 key = key_of(p.col, p.row);
            if (!p.elevation || is_ground_queue(p))
            {
                out.paved.insert(key);
                continue;
            }
            const save::model::PathElevation& e = *p.elevation;
            const f32 low = static_cast<f32>(e.quantised_height) - (slope_falls(e.slope) ? path_rise(e.slope) : 0);
            const auto [at, fresh] = out.decks.try_emplace(key, low);
            if (!fresh)
                at->second = std::min(at->second, low);
        }
        return out;
    }

    f32 limit_ground(save::model::TerrainGrid& grid, const save::model::TerrainGrid& was)
    {
        f32 trimmed = 0;
        for (usize i = 0; i < grid.cells.size() && i < was.cells.size(); ++i)
        {
            for (usize k = 0; k < 4; ++k)
            {
                f32& now = grid.cells[i].corners[k];
                const f32 before = was.cells[i].corners[k];
                const f32 kept = std::clamp(now, std::min(kLowestGround, before), std::max(kHighestGround, before));
                trimmed += std::abs(now - kept);
                now = kept;
            }
        }
        return trimmed;
    }

    bool refuses(const GroundHolds& holds, save::model::Tile tile, const std::array<f32, 4>& was, const std::array<f32, 4>& now)
    {
        const u32 key = key_of(tile.col, tile.row);
        if (holds.held.contains(key))
            return true;
        const auto tilt = [](const std::array<f32, 4>& c) { return std::ranges::max(c) - std::ranges::min(c); };
        // A paved tile already steeper may still be made flatter.
        if (holds.paved.contains(key) && tilt(now) > kPavedTilt && tilt(now) > tilt(was) + kSame)
            return true;
        const auto deck = holds.decks.find(key);
        const auto spans = holds.raised.tiles.find(key);
        for (usize k = 0; k < 4; ++k)
        {
            if (now[k] <= was[k] + kSame)
                continue;
            if (deck != holds.decks.end() && now[k] > deck->second - kDeckRoom)
                return true;
            if (spans != holds.raised.tiles.end() && std::ranges::any_of(spans->second, [&](const path::SolidSpan& s) { return was[k] <= s.low - s.below + kSame && now[k] > s.low - s.below + kSame; }))
                return true;
        }
        return false;
    }

    bool cuts_refused(std::span<const GroundCut> cuts, const save::model::TerrainGrid& grid, const GroundHolds& holds, std::span<const save::model::WaterBody> bodies)
    {
        return std::ranges::any_of(cuts, [&](const GroundCut& cut) {
            const auto [col, row] = cut.tile;
            const auto wet = [&](i32 c, i32 r) { return water::holds_water(bodies, { c, r }); };
            return !owned_outright(grid.at(col, row)) || wet(col, row) || wet(col - 1, row) || wet(col + 1, row) || wet(col, row - 1) || wet(col, row + 1) || refuses(holds, cut.tile, grid.at(col, row).corners, cut.corners);
        });
    }

    bool allow_cuts(GroundCuts& cuts, const save::model::TerrainGrid& grid, const GroundHolds& holds, std::span<const save::model::WaterBody> bodies)
    {
        const auto refused = [&](const GroundCut& cut) { return cuts_refused(std::span(&cut, 1), grid, holds, bodies); };
        if (std::ranges::any_of(cuts.footprint, refused))
        {
            cuts = { };
            return false;
        }
        std::erase_if(cuts.rim, refused);
        return true;
    }

    units::Money cuts_cost(const GroundCuts& cuts, const save::model::TerrainGrid& grid)
    {
        f32 moved = 0;
        for (const std::vector<GroundCut>* part : { &cuts.footprint, &cuts.rim })
            for (const GroundCut& cut : *part)
                for (usize k = 0; k < 4; ++k)
                    moved += std::abs(cut.corners[k] - grid.at(cut.tile.col, cut.tile.row).corners[k]);
        return units::Money { std::lround(moved * kShapeCost) };
    }

    void make_cuts(save::model::TerrainGrid& grid, const GroundCuts& cuts)
    {
        for (const std::vector<GroundCut>* part : { &cuts.footprint, &cuts.rim })
            for (const GroundCut& cut : *part)
                grid.at(cut.tile.col, cut.tile.row).corners = cut.corners;
    }

    bool seat_followers(std::span<save::model::PlacedObject> objects, const GroundHolds& holds, const object::ObjectModels& models, const save::model::TerrainGrid& grid, save::model::Tile first, save::model::Tile last)
    {
        bool moved = false;
        for (const usize i : holds.followers)
        {
            save::model::PlacedObject& o = objects[i];
            const auto model = models.find(object::model_ref(o));
            if (model == models.end())
                continue;
            const object::ObjectModel& m = model->second;
            const auto tiles = under(o, m);
            if (std::ranges::none_of(tiles, [&](const auto& t) { return t.col >= first.col && t.col <= last.col && t.row >= first.row && t.row <= last.row; }))
                continue;
            // Whole units, rounded up so it stands on the ground, not in it.
            const f32 ground = object::object_transform(o, static_cast<object::Positioning>(m.positioning), grid, m.squares, true)[3].y - o.height_adjust.value_or(0);
            o.base_height = static_cast<i32>(std::ceil(ground - kSame));
            moved = true;
        }
        return moved;
    }

} // namespace rct3::park::ground
