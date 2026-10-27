// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_holds.hpp"

#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/item_category.hpp"
#include "park/object_placement.hpp"
#include "park/path_furniture.hpp"
#include "park/queue_fences.hpp"
#include "park/support_columns.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    namespace
    {
        constexpr float kSame = 0.001f; ///< Heights closer than this have not moved

        std::uint32_t key_of(int col, int row)
        {
            return static_cast<std::uint32_t>(row) << 16 | static_cast<std::uint16_t>(col);
        }

        std::vector<std::pair<int, int>> under(const save::PlacedObject& o, const ObjectModel& m)
        {
            SupportFootprint shape;
            shape.x_squares = m.squares[0];
            shape.z_squares = m.squares[1];
            return footprint_tiles(o, shape);
        }
    } // namespace

    GroundHolds ground_holds(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        GroundHolds out;
        std::vector<save::PlacedObject> raised;
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            const save::PlacedObject& o = objects[i];
            if (o.hidden || o.fixture)
                continue;
            const auto model = models.find(model_ref(o));
            if (model == models.end())
                continue;
            const ObjectModel& m = model->second;
            if (m.holds_ground)
            {
                for (const auto [c, r] : under(o, m))
                    out.held.insert(key_of(c, r));
            }
            else if (on_ground(o) && is_prop(m.category) && !on_paths(static_cast<Positioning>(m.positioning)))
                out.followers.push_back(i);
            else if (!m.solid.empty())
                raised.push_back(o);
        }
        out.raised = solid_tiles(raised, models, grid);
        for (const save::PathTile& p : paths)
        {
            if (p.elevation && p.elevation->underground)
                continue;
            const std::uint32_t key = key_of(p.col, p.row);
            if (!p.elevation || is_ground_queue(p))
            {
                out.paved.insert(key);
                continue;
            }
            const save::PathElevation& e = *p.elevation;
            const float low = static_cast<float>(e.quantised_height) - (slope_falls(e.slope) ? path_rise(e.slope) : 0);
            const auto [at, fresh] = out.decks.try_emplace(key, low);
            if (!fresh)
                at->second = std::min(at->second, low);
        }
        return out;
    }

    bool refuses(const GroundHolds& holds, save::Tile tile, const std::array<float, 4>& was, const std::array<float, 4>& now)
    {
        const std::uint32_t key = key_of(tile.col, tile.row);
        if (holds.held.contains(key))
            return true;
        const auto tilt = [](const std::array<float, 4>& c) { return std::ranges::max(c) - std::ranges::min(c); };
        // A paved tile already steeper may still be made flatter.
        if (holds.paved.contains(key) && tilt(now) > kPavedTilt && tilt(now) > tilt(was) + kSame)
            return true;
        const auto deck = holds.decks.find(key);
        const auto spans = holds.raised.tiles.find(key);
        for (std::size_t k = 0; k < 4; ++k)
        {
            if (now[k] <= was[k] + kSame)
                continue;
            if (deck != holds.decks.end() && now[k] > deck->second - kDeckRoom)
                return true;
            if (spans != holds.raised.tiles.end() && std::ranges::any_of(spans->second, [&](const SolidSpan& s) { return was[k] <= s.low + kSame && now[k] > s.low + kSame; }))
                return true;
        }
        return false;
    }

    bool seat_followers(std::span<save::PlacedObject> objects, const GroundHolds& holds, const ObjectModels& models, const save::TerrainGrid& grid, save::Tile first, save::Tile last)
    {
        bool moved = false;
        for (const std::size_t i : holds.followers)
        {
            save::PlacedObject& o = objects[i];
            const auto model = models.find(model_ref(o));
            if (model == models.end())
                continue;
            const ObjectModel& m = model->second;
            const auto tiles = under(o, m);
            if (std::ranges::none_of(tiles, [&](const auto& t) { return t.first >= first.col && t.first <= last.col && t.second >= first.row && t.second <= last.row; }))
                continue;
            // Whole units, rounded up so it stands on the ground, not in it.
            const float ground = object_transform(o, static_cast<Positioning>(m.positioning), grid, m.squares, true)[3].y - o.height_adjust.value_or(0);
            o.base_height = static_cast<std::int32_t>(std::ceil(ground - kSame));
            moved = true;
        }
        return moved;
    }

} // namespace rct3::park
