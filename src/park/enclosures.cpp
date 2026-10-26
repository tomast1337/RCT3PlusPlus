// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/enclosures.hpp"

#include "park/ground_queues.hpp"
#include "park/queue_fences.hpp"

#include <algorithm>
#include <format>
#include <vector>

namespace rct3::park
{

    std::pair<std::string, std::string> fence_piece(const std::string& fence)
    {
        return { fence + "Edge", std::format("Enclosures/Fences/{0}/{0}_Stub", fence) };
    }

    BoundaryFence enclosure_fences(const save::EnclosureMap& map, const save::TerrainGrid& grid, std::uint64_t last_id)
    {
        BoundaryFence out;
        for (std::int32_t row = 0; row < map.rows; ++row)
        {
            for (std::int32_t col = 0; col < map.cols; ++col)
            {
                const std::int32_t own = map.at(col, row);
                if (own < 0)
                    continue;
                const auto [symbol, overlay] = fence_piece(map.fences[static_cast<std::size_t>(own)]);
                for (const TileSide side : { TileSide::Back, TileSide::Right, TileSide::Front, TileSide::Left })
                {
                    const auto [dc, dr] = side_step(side);
                    const std::int32_t other = map.at(col + dc, row + dr);
                    // Between two enclosures the first one's fence stands.
                    if (other == own || (other >= 0 && other < own))
                        continue;
                    save::PlacedObject& o = out.pieces.emplace_back();
                    o.id = ++last_id;
                    o.owner_ref = o.id;
                    o.fixture = true;
                    o.symbol = symbol;
                    o.overlay = overlay;
                    o.col = col;
                    o.row = row;
                    o.rotation = edge_turns(side);
                    if (grid.contains(col, row))
                        if (const glm::vec2 lean = panel_lean(grid, o); lean != glm::vec2(0.0f))
                            out.leans.emplace(o.id, lean);
                }
            }
        }
        return out;
    }

    std::int32_t enclose(save::EnclosureMap& map, const save::TerrainGrid& grid, save::Tile a, save::Tile b, const std::string& fence)
    {
        if (map.tiles.empty())
            map = { .cols = grid.width, .rows = grid.height, .tiles = std::vector<std::int32_t>(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), -1), .fences = map.fences };
        const std::int32_t c0 = std::max(0, std::min(a.col, b.col)), c1 = std::min(map.cols - 1, std::max(a.col, b.col));
        const std::int32_t r0 = std::max(0, std::min(a.row, b.row)), r1 = std::min(map.rows - 1, std::max(a.row, b.row));
        // The enclosures the area takes in or touches along a side.
        std::vector<std::int32_t> joined;
        for (std::int32_t row = r0 - 1; row <= r1 + 1; ++row)
            for (std::int32_t col = c0 - 1; col <= c1 + 1; ++col)
            {
                const bool corner = (row < r0 || row > r1) && (col < c0 || col > c1);
                if (const std::int32_t r = map.at(col, row); !corner && r >= 0 && !std::ranges::contains(joined, r))
                    joined.push_back(r);
            }
        std::int32_t region = 0;
        if (!joined.empty())
            region = std::ranges::min(joined);
        else if (const auto unused = std::ranges::find(map.fences, std::string()); unused != map.fences.end())
        {
            region = static_cast<std::int32_t>(unused - map.fences.begin());
            *unused = fence;
        }
        else
        {
            region = static_cast<std::int32_t>(map.fences.size());
            map.fences.push_back(fence);
        }
        for (std::int32_t& t : map.tiles)
            if (std::ranges::contains(joined, t))
                t = region;
        for (const std::int32_t r : joined)
            if (r != region)
                map.fences[static_cast<std::size_t>(r)].clear();
        for (std::int32_t row = r0; row <= r1; ++row)
            for (std::int32_t col = c0; col <= c1; ++col)
                map.tiles[static_cast<std::size_t>(row * map.cols + col)] = region;
        return region;
    }

    void unenclose(save::EnclosureMap& map, save::Tile a, save::Tile b)
    {
        for (std::int32_t row = std::max(0, std::min(a.row, b.row)); row <= std::min(map.rows - 1, std::max(a.row, b.row)); ++row)
            for (std::int32_t col = std::max(0, std::min(a.col, b.col)); col <= std::min(map.cols - 1, std::max(a.col, b.col)); ++col)
                map.tiles[static_cast<std::size_t>(row * map.cols + col)] = -1;
        for (std::size_t r = 0; r < map.fences.size(); ++r)
            if (enclosure_size(map, static_cast<std::int32_t>(r)) == 0)
                map.fences[r].clear();
    }

    std::int32_t enclosure_size(const save::EnclosureMap& map, std::int32_t region) noexcept
    {
        return static_cast<std::int32_t>(std::ranges::count(map.tiles, region));
    }

    std::int32_t enclosure_under(const save::EnclosureMap& map, save::Tile corner, std::array<std::int32_t, 2> span) noexcept
    {
        const std::int32_t first = map.at(corner.col, corner.row);
        for (std::int32_t row = corner.row; row < corner.row + span[1]; ++row)
            for (std::int32_t col = corner.col; col < corner.col + span[0]; ++col)
                if (map.at(col, row) != first)
                    return -1;
        return first;
    }

} // namespace rct3::park
