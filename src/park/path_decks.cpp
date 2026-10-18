// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_decks.hpp"

#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/queue_fences.hpp"
#include "park/terrain_mesh.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace rct3::park
{

    namespace
    {

        constexpr std::uint8_t kSides = 4;

        /// A piece unturned: its open sides and filled corners, a bit each.
        struct Pattern
        {
            DeckPiece piece;
            std::uint8_t sides;
            std::uint8_t corners;
        };

        constexpr std::array<Pattern, 15> kPatterns { {
            { DeckPiece::Flat, 0b0000, 0b0000 },
            { DeckPiece::TurnU, 0b0010, 0b0000 },
            { DeckPiece::StraightA, 0b1010, 0b0000 },
            { DeckPiece::TurnLA, 0b0110, 0b0000 },
            { DeckPiece::TurnLB, 0b0110, 0b0010 },
            { DeckPiece::TurnTA, 0b1011, 0b0000 },
            { DeckPiece::TurnTB, 0b1011, 0b1000 },
            { DeckPiece::TurnTC, 0b1011, 0b0001 },
            { DeckPiece::StraightB, 0b1011, 0b1001 },
            { DeckPiece::TurnX, 0b1111, 0b0000 },
            { DeckPiece::CornerD, 0b1111, 0b0010 },
            { DeckPiece::CornerB, 0b1111, 0b1001 },
            { DeckPiece::CornerC, 0b1111, 0b1010 },
            { DeckPiece::CornerA, 0b1111, 0b1101 },
            { DeckPiece::Flat, 0b1111, 0b1111 },
        } };

        /// The corners of a tile (in the terrain's order) on each side's edge.
        constexpr std::array<std::array<std::size_t, 2>, kSides> kEdgeCorners { { { 2, 3 }, { 1, 3 }, { 0, 1 }, { 0, 2 } } };

        constexpr std::uint8_t rotated_bits(std::uint8_t bits, std::uint8_t turns) noexcept
        {
            return static_cast<std::uint8_t>(((bits << turns) | (bits >> (kSides - turns))) & 0b1111);
        }

        constexpr std::uint8_t next_side(std::uint8_t side, std::uint8_t by) noexcept
        {
            return static_cast<std::uint8_t>((side + by) % kSides);
        }

        std::array<int, 2> across(int col, int row, std::uint8_t side) noexcept
        {
            constexpr std::array<std::array<int, 2>, kSides> kSteps { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } };
            return { col + kSteps[side][0], row + kSteps[side][1] };
        }

        bool meets(float a, float b) noexcept
        {
            return std::abs(a - b) <= kPathMeets;
        }

        /// The paths by tile, to find a tile's paths without looking through them all.
        class TilePaths
        {
        public:
            explicit TilePaths(std::span<const save::PathTile> paths)
            {
                for (const save::PathTile& p : paths)
                    by_tile_[key(p.col, p.row)].push_back(&p);
            }

            std::span<const save::PathTile* const> at(int col, int row) const
            {
                if (col < 0 || row < 0)
                    return { };
                const auto found = by_tile_.find(key(col, row));
                return found == by_tile_.end() ? std::span<const save::PathTile* const> { } : std::span(found->second);
            }

        private:
            static std::uint32_t key(int col, int row) noexcept
            {
                return static_cast<std::uint32_t>(col) << 16 | static_cast<std::uint32_t>(row);
            }

            std::unordered_map<std::uint32_t, std::vector<const save::PathTile*>> by_tile_;
        };

        /// Whether `path` is flat path (not queue) at `height`.
        bool flat_at(const save::PathTile& path, const save::TerrainGrid& grid, float height)
        {
            if (path.kind == save::PathKind::Queue)
                return false;
            if (path.elevation)
                return path.elevation->slope == 0 && meets(static_cast<float>(path.elevation->quantised_height), height);
            if (path.kind != save::PathKind::Ground || !grid.contains(path.col, path.row))
                return false;
            const save::TerrainCell& cell = grid.at(path.col, path.row);
            return cell.max_height() - cell.min_height() <= kFlat && meets(cell.min_height(), height);
        }

        bool any_flat_at(const TilePaths& tiles, const save::TerrainGrid& grid, std::array<int, 2> tile, float height)
        {
            return std::ranges::any_of(tiles.at(tile[0], tile[1]), [&](const save::PathTile* p) { return flat_at(*p, grid, height); });
        }

        Deck choose(const save::PathTile& path, const TilePaths& tiles, const save::TerrainGrid& grid)
        {
            const save::PathElevation& e = *path.elevation;
            const auto height = static_cast<float>(e.quantised_height);
            if (e.slope != 0)
            {
                const std::uint8_t heading = stored_direction(path.direction);
                // A gentle slope beside: the same slope, the same way, from the same height.
                const auto beside = [&](std::uint8_t side) {
                    const auto [col, row] = across(path.col, path.row, side);
                    return std::ranges::any_of(tiles.at(col, row), [&](const save::PathTile* p) { return p->kind != save::PathKind::Queue && p->elevation && p->elevation->slope == e.slope && p->direction == path.direction && p->elevation->quantised_height == e.quantised_height; });
                };
                return slope_deck(e.slope, heading, beside(next_side(heading, 3)), beside(next_side(heading, 1)));
            }
            std::uint8_t sides = 0;
            for (std::uint8_t side = 0; side < kSides; ++side)
            {
                const auto [col, row] = across(path.col, path.row, side);
                const std::uint8_t facing = next_side(side, 2);
                const bool joined = std::ranges::any_of(tiles.at(col, row), [&](const save::PathTile* p) {
                    const auto edge = edge_height(*p, grid, facing);
                    return edge && meets(*edge, height);
                });
                if (joined)
                    sides = static_cast<std::uint8_t>(sides | 1u << side);
            }
            std::uint8_t corners = 0;
            for (std::uint8_t corner = 0; corner < kSides; ++corner)
            {
                const std::uint8_t other = next_side(corner, 1);
                if ((sides >> corner & 1u) == 0 || (sides >> other & 1u) == 0)
                    continue;
                const auto a = across(path.col, path.row, corner);
                const auto b = across(path.col, path.row, other);
                if (any_flat_at(tiles, grid, a, height) && any_flat_at(tiles, grid, b, height) && any_flat_at(tiles, grid, across(a[0], a[1], other), height))
                    corners = static_cast<std::uint8_t>(corners | 1u << corner);
            }
            return flat_deck(sides, corners);
        }

    } // namespace

    Deck flat_deck(std::uint8_t sides, std::uint8_t corners) noexcept
    {
        sides &= 0b1111;
        // Only corners between two joined sides count.
        corners &= static_cast<std::uint8_t>(sides & rotated_bits(sides, 3));
        for (std::uint8_t turns = 0; turns < kSides; ++turns)
        {
            for (const Pattern& p : kPatterns)
            {
                if (rotated_bits(p.sides, turns) == sides && rotated_bits(p.corners, turns) == corners)
                    return { p.piece, turns };
            }
        }
        return { };
    }

    Deck slope_deck(std::uint8_t slope, std::uint8_t heading, bool before, bool after) noexcept
    {
        const bool up = !slope_falls(slope);
        const auto turns = next_side(heading, up ? 3 : 1);
        if (path_rise(slope) > 1)
            return { DeckPiece::Slope, turns };
        // The slope beside this one on the left going up is on the right going down.
        const bool left = up ? after : before;
        const bool right = up ? before : after;
        const DeckPiece piece = left && right ? DeckPiece::SlopeMid : left ? DeckPiece::SlopeStraightLeft : right ? DeckPiece::SlopeStraightRight : DeckPiece::SlopeStraight;
        return { piece, turns };
    }

    std::uint8_t stored_direction(std::uint8_t heading) noexcept
    {
        constexpr std::array<std::uint8_t, kSides> kSwap { 3, 1, 2, 0 };
        return heading < kSides ? kSwap[heading] : heading;
    }

    std::array<std::size_t, 2> edge_corners(std::uint8_t side) noexcept
    {
        return kEdgeCorners[side % kSides];
    }

    std::optional<float> edge_height(const save::PathTile& path, const save::TerrainGrid& grid, std::uint8_t side)
    {
        if (side >= kSides || !grid.contains(path.col, path.row))
            return std::nullopt;
        const auto& corners = grid.at(path.col, path.row).corners;
        const float ground = (corners[kEdgeCorners[side][0]] + corners[kEdgeCorners[side][1]]) / 2;
        // A queue reaches only the sides its line comes in and goes out by.
        const bool queue_side = path.queue && (stored_direction(path.queue->start_direction) == side || stored_direction(path.queue->end_direction) == side);
        if (is_ground_queue(path))
            return queue_side ? std::optional(ground) : std::nullopt;
        if (!path.elevation)
            return path.kind == save::PathKind::Ground ? std::optional(ground) : std::nullopt;
        const save::PathElevation& e = *path.elevation;
        const auto height = static_cast<float>(e.quantised_height);
        if (e.slope == 0)
            return path.kind != save::PathKind::Queue || queue_side ? std::optional(height) : std::nullopt;
        const std::uint8_t heading = stored_direction(path.direction);
        if (side == heading)
            return height + (slope_falls(e.slope) ? -path_rise(e.slope) : path_rise(e.slope));
        if (side == next_side(heading, 2))
            return height;
        return std::nullopt;
    }

    std::string deck_symbol(const PathStyle& style, DeckPiece piece)
    {
        const auto at = static_cast<std::size_t>(piece);
        return at < style.pieces.size() ? style.pieces[at] : std::string();
    }

    Deck choose_deck(const save::PathTile& path, std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        if (!path.elevation)
            return { };
        return choose(path, TilePaths(paths), grid);
    }

    std::size_t refresh_decks(std::span<const save::PathTile> paths, std::vector<save::PlacedObject>& objects, const save::TerrainGrid& grid, std::span<const PathStyle> styles, int low_col, int low_row, int high_col, int high_row)
    {
        const TilePaths tiles(paths);
        std::unordered_map<std::uint64_t, save::PlacedObject*> by_id;
        for (save::PlacedObject& o : objects)
            by_id.emplace(o.id, &o);
        std::size_t changed = 0;
        for (int col = low_col; col <= high_col; ++col)
        {
            for (int row = low_row; row <= high_row; ++row)
            {
                for (const save::PathTile* p : tiles.at(col, row))
                {
                    if (p->kind != save::PathKind::Flying || !p->elevation)
                        continue;
                    const auto object = by_id.find(p->elevation->scenery_ref);
                    const auto style = std::ranges::find_if(styles, [&](const PathStyle& s) { return !s.queue && strings::equals_ignoring_case(s.name, p->surface_name); });
                    if (object == by_id.end() || style == styles.end())
                        continue;
                    const Deck deck = choose(*p, tiles, grid);
                    std::string symbol = deck_symbol(*style, deck.piece);
                    if (symbol.empty() || (symbol == object->second->symbol && object->second->rotation == deck.turns))
                        continue;
                    object->second->symbol = std::move(symbol);
                    object->second->rotation = deck.turns;
                    ++changed;
                }
            }
        }
        return changed;
    }

} // namespace rct3::park
