// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_paths.hpp"

#include "park/ground_queues.hpp"
#include "park/object_placement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace rct3::park
{

    PathCell path_cell(PathLook look) noexcept
    {
        switch (look)
        {
            case PathLook::EdgeXJoined:
                return { 0, 0 };
            case PathLook::CornerOnly:
                return { 0, 1 };
            case PathLook::Open:
                return { 0, 2 };
            case PathLook::EdgeZJoined:
                return { 0, 3 };
            case PathLook::EdgeX:
                return { 1, 0 };
            case PathLook::Closed:
                return { 1, 1 };
            case PathLook::Notched:
                return { 1, 2 };
            case PathLook::EdgeZ:
                return { 1, 3 };
        }
        return { 0, 2 };
    }

    PathLook quarter_look(bool along_x, bool along_z, bool across) noexcept
    {
        if (along_x && along_z)
            return across ? PathLook::Open : PathLook::Notched;
        if (along_z)
            return across ? PathLook::EdgeXJoined : PathLook::EdgeX;
        if (along_x)
            return across ? PathLook::EdgeZJoined : PathLook::EdgeZ;
        return PathLook::Closed;
    }

    float path_rise(std::uint8_t slope) noexcept
    {
        switch (slope)
        {
            case 1:
            case 2:
                return 2; // Steep
            case 4:
            case 5:
                return 1; // Gentle
            default:
                return 0;
        }
    }

    GroundPathMap ground_path_map(std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        GroundPathMap map;
        map.width = grid.width;
        map.height = grid.height;
        map.tiles.assign(static_cast<std::size_t>(std::max(grid.width, 0)) * static_cast<std::size_t>(std::max(grid.height, 0)), kNoPath);
        for (const save::PathTile& p : paths)
        {
            if (!grid.contains(p.col, p.row))
                continue;
            const std::size_t at = static_cast<std::size_t>(p.row) * static_cast<std::size_t>(map.width) + p.col;
            if (is_ground_queue(p))
            {
                auto& sides = map.queues[at];
                for (const std::uint8_t d : { p.queue->start_direction, p.queue->end_direction })
                {
                    if (const auto side = side_of(d))
                        sides = static_cast<std::uint8_t>(sides | 1u << static_cast<unsigned>(*side));
                }
                continue;
            }
            if (p.elevation)
            {
                // Which end the stored height is at is not needed: either end may meet the ground.
                const auto stored = static_cast<float>(p.elevation->quantised_height);
                const float rise = path_rise(p.elevation->slope);
                auto& ends = map.raised[at];
                for (const float h : { stored - rise, stored, stored + rise })
                    ends.push_back(h);
                continue;
            }
            if (p.kind != save::PathKind::Ground || p.surface_name.empty())
                continue;
            auto style = std::ranges::find(map.styles, p.surface_name);
            if (style == map.styles.end())
            {
                if (map.styles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
                    continue;
                map.styles.push_back(p.surface_name);
                style = std::prev(map.styles.end());
            }
            map.tiles[at] = static_cast<std::int16_t>(style - map.styles.begin());
        }
        return map;
    }

    std::vector<Paving> ride_pavings(const save::RideModel& rides, const FootprintOf& footprint_of, const FootprintOf& floor_of)
    {
        const auto step = [](save::Tile tile, glm::vec2 way) { return save::Tile { tile.col + static_cast<int>(std::lround(way.x)), tile.row + static_cast<int>(std::lround(way.y)) }; };
        std::vector<Paving> out;
        for (const save::Ride& ride : rides.rides)
        {
            if (ride.base.demolished)
                continue;
            // A door faces the ride: it opens onto the tile in front and the path behind, and takes that path's look.
            for (const save::Station& station : ride.stations)
            {
                for (const auto& [door, entrance] : { std::pair { station.entrance_spot, true }, { station.exit_spot, false } })
                {
                    if (!door)
                        continue;
                    const save::Tile at { door->col, door->row };
                    const save::Tile behind = step(at, turned_x(door->direction + 2));
                    out.push_back({ .tiles = { at }, .height = door->height, .source = behind, .entrance = entrance, .links = { step(at, turned_x(door->direction)), behind } });
                }
            }
            if (ride.kind == save::RideKind::Special && ride.stations.empty() && ride.base.spot)
            {
                // A facility without doors (toilets and the like) opens onto every side, in the plain style.
                Paving under { .tiles = footprint_of(*ride.base.spot), .height = ride.base.spot->height };
                for (const save::Tile tile : under.tiles)
                {
                    for (std::int32_t side = 0; side < 4; ++side)
                    {
                        const save::Tile there = step(tile, turned_x(side));
                        if (!std::ranges::contains(under.tiles, there) && !std::ranges::contains(under.links, there))
                            under.links.push_back(there);
                    }
                }
                if (!under.tiles.empty())
                    out.push_back(std::move(under));
                continue;
            }
            // A flat ride's floor, or a tracked ride's base's, opens onto its first station's doors and looks like
            // the path its exit lets out onto.
            const auto pave = [&](std::vector<save::Tile> tiles, std::int32_t height) {
                Paving floor { .tiles = std::move(tiles), .height = height };
                if (floor.tiles.empty())
                    return;
                if (!ride.stations.empty())
                {
                    const save::Station& first = ride.stations.front();
                    for (const auto& door : { first.exit_spot, first.entrance_spot })
                    {
                        if (!door)
                            continue;
                        if (!floor.source)
                            floor.source = step({ door->col, door->row }, turned_x(door->direction + 2));
                        floor.links.push_back({ door->col, door->row });
                    }
                }
                out.push_back(std::move(floor));
            };
            if (ride.kind == save::RideKind::Flat && ride.base.spot)
                pave(footprint_of(*ride.base.spot), ride.base.spot->height);
            if (ride.track)
            {
                for (const save::TrackPiece& piece : ride.track->pieces)
                {
                    if (piece.scenery != 0)
                        pave(floor_of({ piece.scenery, piece.tile_x, piece.tile_z, piece.height, piece.direction }), piece.height);
                }
            }
        }
        for (const save::Stall& stall : rides.stalls)
        {
            if (stall.base.demolished || !stall.base.spot)
                continue;
            Paving under { .tiles = footprint_of(*stall.base.spot), .height = stall.base.spot->height };
            // A stall serves the side it faces, opening onto the tiles there. Facings point at -row, -col, +row, +col.
            const glm::vec2 front = turned_x(stall.base.facing + 3);
            for (const save::Tile tile : under.tiles)
            {
                const save::Tile there = step(tile, front);
                if (!std::ranges::contains(under.tiles, there) && !std::ranges::contains(under.links, there))
                    under.links.push_back(there);
            }
            if (under.tiles.empty())
                continue;
            under.source = under.links.front();
            out.push_back(std::move(under));
        }
        return out;
    }

    namespace
    {

        /// Whether `p` lies at `height` on its tile, near enough to set a paving's style.
        bool lies_at(const save::PathTile& p, std::int32_t height, const save::TerrainGrid& grid)
        {
            const auto near = [&](float h) { return std::abs(h - static_cast<float>(height)) <= kPavingReach; };
            if (!p.elevation)
                return grid.contains(p.col, p.row) && std::ranges::any_of(grid.at(p.col, p.row).corners, near);
            const auto stored = static_cast<float>(p.elevation->quantised_height);
            const float rise = path_rise(p.elevation->slope);
            return near(stored - rise) || near(stored) || near(stored + rise);
        }

    } // namespace

    std::vector<PavedTile> lay_pavings(std::span<const save::PathTile> paths, std::span<const Paving> pavings, const save::TerrainGrid& grid)
    {
        std::vector<PavedTile> out;
        for (std::size_t k = 0; k < pavings.size(); ++k)
        {
            const Paving& paving = pavings[k];
            // The path at the source sets the look: a path of the park as it is, a queue only through an entrance.
            save::PathTile look { .kind = save::PathKind::Ground, .surface_name = std::string(kPavingStyle) };
            if (paving.source)
            {
                const auto at_source = [&](const save::PathTile& p) { return p.col == paving.source->col && p.row == paving.source->row && lies_at(p, paving.height, grid); };
                const auto path = std::ranges::find_if(paths, [&](const save::PathTile& p) { return at_source(p) && !p.queue && !p.surface_name.empty(); });
                const auto queue = std::ranges::find_if(paths, [&](const save::PathTile& p) { return at_source(p) && p.queue && !p.surface_name.empty(); });
                if (path != paths.end())
                    look = *path;
                else if (paving.entrance && queue != paths.end())
                    look = *queue;
            }
            look.id = 0;
            look.direction = save::kNoDirection;
            for (const save::Tile tile : paving.tiles)
            {
                if (!grid.contains(tile.col, tile.row) || std::ranges::any_of(paths, [&](const save::PathTile& p) { return p.col == tile.col && p.row == tile.row; }))
                    continue;
                save::PathTile laid = look;
                laid.col = static_cast<std::uint8_t>(tile.col);
                laid.row = static_cast<std::uint8_t>(tile.row);
                const bool level = std::ranges::all_of(grid.at(tile.col, tile.row).corners, [&](float h) { return h == static_cast<float>(paving.height); });
                if (laid.queue)
                {
                    // Straight on from the source; stored headings run towards lower columns, higher columns,
                    // lower rows, higher rows.
                    const save::Tile from = *paving.source;
                    const std::uint8_t in = from.col > tile.col ? 0 : from.col < tile.col ? 1 : from.row > tile.row ? 2 : 3;
                    laid.queue->start_direction = in;
                    laid.queue->end_direction = in;
                }
                if (level && !laid.queue)
                    laid.elevation.reset();
                else
                    laid.elevation = save::PathElevation { .slope = level ? save::kSlopeOnGround : std::uint8_t { 0 }, .quantised_height = paving.height };
                out.push_back({ .path = std::move(laid), .paving = k });
            }
        }
        return out;
    }

    void seal_pavings(GroundPathMap& map, std::span<const PavedTile> laid, std::span<const Paving> pavings)
    {
        const auto index = [&](save::Tile t) -> std::optional<std::size_t> {
            if (t.col < 0 || t.row < 0 || t.col >= map.width || t.row >= map.height)
                return std::nullopt;
            return static_cast<std::size_t>(t.row) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(t.col);
        };
        if (laid.empty())
            return;
        map.pavings.assign(map.tiles.size(), 0);
        for (const PavedTile& t : laid)
        {
            const auto at = index({ t.path.col, t.path.row });
            if (!at || t.paving >= pavings.size())
                continue;
            map.pavings[*at] = static_cast<std::uint16_t>(std::min<std::size_t>(t.paving + 1, std::numeric_limits<std::uint16_t>::max()));
            for (const save::Tile link : pavings[t.paving].links)
            {
                if (const auto other = index(link); other && std::abs(link.col - t.path.col) + std::abs(link.row - t.path.row) == 1)
                {
                    map.links.emplace(*at, *other);
                    map.links.emplace(*other, *at);
                }
            }
        }
    }

    bool may_join(const GroundPathMap& map, save::Tile tile, save::Tile other) noexcept
    {
        if (map.pavings.empty())
            return true;
        const auto index = [&](save::Tile t) -> std::optional<std::size_t> {
            if (t.col < 0 || t.row < 0 || t.col >= map.width || t.row >= map.height)
                return std::nullopt;
            return static_cast<std::size_t>(t.row) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(t.col);
        };
        const auto a = index(tile);
        const auto b = index(other);
        const std::uint16_t pa = a ? map.pavings[*a] : 0;
        const std::uint16_t pb = b ? map.pavings[*b] : 0;
        if (pa == pb)
            return true;
        return a && b && map.links.contains({ *a, *b });
    }

    bool has_ground_path(const GroundPathMap& map, save::Tile tile) noexcept
    {
        const auto [col, row] = tile;
        if (col < 0 || row < 0 || col >= map.width || row >= map.height)
            return false;
        return map.tiles[static_cast<std::size_t>(row) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(col)] != kNoPath;
    }

    bool path_continues(const GroundPathMap& map, const save::TerrainGrid& grid, save::Tile tile, int dc, int dr)
    {
        const auto [col, row] = tile;
        if (!grid.contains(col, row) || !grid.contains(col + dc, row + dr))
            return has_ground_path(map, { col + dc, row + dr });
        // This tile's two corners on the shared edge, and the side it is as path_decks numbers them.
        const auto& corners = grid.at(col, row).corners;
        const std::size_t a = dc > 0 ? 1 : dr > 0 ? 2 : 0;
        const std::size_t b = dc != 0 ? a + 2 : a + 1;
        const float edge = (corners[a] + corners[b]) / 2;
        const std::uint8_t side = dc > 0 ? 1 : dc < 0 ? 3 : dr > 0 ? 0 : 2;
        if (!may_join(map, tile, { col + dc, row + dr }))
            return false;
        // Paving runs on within itself and into its links whatever stands between: the ride's fence is its edge.
        const std::size_t here = static_cast<std::size_t>(row) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(col);
        const bool paved = !map.pavings.empty() && (map.pavings[here] != 0 || map.pavings[here + static_cast<std::size_t>(dr * map.width + dc)] != 0);
        const bool wall = !paved && walled(map.walls, tile, side, edge);
        if (has_ground_path(map, { col + dc, row + dr }))
            return !wall;
        const std::size_t there = static_cast<std::size_t>(row + dr) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(col + dc);
        if (const auto queue = map.queues.find(there); queue != map.queues.end())
        {
            // The neighbour's side facing back towards this tile.
            const TileSide facing = dc > 0 ? TileSide::Left : dc < 0 ? TileSide::Right : dr > 0 ? TileSide::Back : TileSide::Front;
            if ((queue->second & 1u << static_cast<unsigned>(facing)) != 0)
                return true;
        }
        const auto found = map.raised.find(there);
        if (found == map.raised.end() || wall)
            return false;
        return std::ranges::any_of(found->second, [&](float h) { return std::abs(h - edge) <= kPathMeets; });
    }

    namespace
    {

        /// A point of a tile, (u, v) from 0 to 1 across it along x and z, on the tile's two ground
        /// triangles: (0, 1, 2) where u + v <= 1, (3, 2, 1) beyond.
        template<typename T> T on_tile(const std::array<T, 4>& at, float u, float v)
        {
            if (u + v <= 1)
                return at[0] + (at[1] - at[0]) * u + (at[2] - at[0]) * v;
            return at[3] + (at[2] - at[3]) * (1 - u) + (at[1] - at[3]) * (1 - v);
        }

        /// The texture coordinates of a span of a cell, (u, v) from 0 to 1 across it. Kept half a texel off
        /// the cell's border so the next cell never bleeds in.
        glm::vec2 cell_uv(std::uint8_t cell, float u, float v)
        {
            constexpr float kInset = 1.0f / 256;
            const glm::vec2 origin { (cell & 1) != 0 ? 0.5f : 0.0f, (cell & 2) != 0 ? 0.5f : 0.0f };
            return origin + glm::vec2(kInset + u * (0.5f - 2 * kInset), kInset + v * (0.5f - 2 * kInset));
        }

        using Key = std::pair<GroundPaint, std::uint8_t>;

        struct Builder
        {
            std::map<Key, std::vector<GroundVertex>> by_paint;

            /// The part of a tile from (u0, v0) to (u1, v1) painted with `look`, split along the same
            /// diagonal as the ground so it lies in the ground's triangles.
            void span(std::uint8_t style, PathLook look, const std::array<glm::vec3, 4>& corners, const std::array<glm::vec3, 4>& normals, float u0, float v0, float u1, float v1)
            {
                const PathCell cell = path_cell(look);
                auto& out = by_paint[{ cell.sheet == 0 ? GroundPaint::PathA : GroundPaint::PathB, style }];
                std::array<GroundVertex, 4> q;
                for (int k = 0; k < 4; ++k)
                {
                    const float u = (k & 1) != 0 ? u1 : u0;
                    const float v = (k & 2) != 0 ? v1 : v0;
                    q[static_cast<std::size_t>(k)] = { on_tile(corners, u, v) + glm::vec3(0.0f, kPathLift, 0.0f), glm::vec4(1.0f), cell_uv(cell.cell, u, v), on_tile(normals, u, v) };
                }
                for (const int k : { 0, 1, 2, 3, 2, 1 })
                    out.push_back(q[static_cast<std::size_t>(k)]);
            }
        };

    } // namespace

    TerrainMesh ground_path_mesh(const GroundPathMap& map, const save::TerrainGrid& grid, std::optional<TileRect> area)
    {
        TerrainMesh mesh;
        if (map.width != grid.width || map.height != grid.height || map.tiles.size() != grid.cells.size())
            return mesh;

        const TileRect tiles = area.value_or(TileRect { 0, 0, grid.width, grid.height });
        Builder b;
        bool any = false;
        for (int row = std::max(tiles.row, 0); row < std::min(tiles.end_row, grid.height); ++row)
        {
            for (int col = std::max(tiles.col, 0); col < std::min(tiles.end_col, grid.width); ++col)
            {
                const std::int16_t style = map.tiles[static_cast<std::size_t>(row) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(col)];
                if (style == kNoPath)
                    continue;
                std::array<PathLook, 4> looks { };
                for (int k = 0; k < 4; ++k)
                {
                    const int dc = (k & 1) != 0 ? 1 : -1;
                    const int dr = (k & 2) != 0 ? 1 : -1;
                    looks[static_cast<std::size_t>(k)] = quarter_look(path_continues(map, grid, { col, row }, dc, 0), path_continues(map, grid, { col, row }, 0, dr), has_ground_path(map, { col + dc, row + dr }) && may_join(map, { col, row }, { col + dc, row + dr }) && may_join(map, { col + dc, row }, { col + dc, row + dr }) && may_join(map, { col, row + dr }, { col + dc, row + dr }));
                }

                const save::TerrainCell& cell = grid.at(col, row);
                std::array<glm::vec3, 4> corners;
                std::array<glm::vec3, 4> normals;
                for (int c = 0; c < 4; ++c)
                {
                    const auto cu = static_cast<std::size_t>(c);
                    corners[cu] = { grid.world_x(col + c % 2), cell.corners[cu], grid.world_z(row + c / 2) };
                    normals[cu] = glm::normalize(ground_normal(grid, { col, row }, c));
                }
                const auto s = static_cast<std::uint8_t>(style);
                if (std::ranges::all_of(looks, [&](PathLook l) { return l == looks[0]; }))
                {
                    b.span(s, looks[0], corners, normals, 0, 0, 1, 1);
                }
                else
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        const float u = (k & 1) != 0 ? 0.5f : 0.0f;
                        const float v = (k & 2) != 0 ? 0.5f : 0.0f;
                        b.span(s, looks[static_cast<std::size_t>(k)], corners, normals, u, v, u + 0.5f, v + 0.5f);
                    }
                }
                const glm::vec3 low { corners[0].x, cell.min_height(), corners[0].z };
                const glm::vec3 high { corners[3].x, cell.max_height() + kPathLift, corners[3].z };
                mesh.low = any ? glm::min(mesh.low, low) : low;
                mesh.high = any ? glm::max(mesh.high, high) : high;
                any = true;
            }
        }

        for (auto& [key, vertices] : b.by_paint)
        {
            GroundBatch batch { key.first, key.second, static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(vertices.size()) };
            for (GroundVertex& v : vertices)
            {
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back(v);
            }
            mesh.batches.push_back(batch);
        }
        return mesh;
    }

} // namespace rct3::park
