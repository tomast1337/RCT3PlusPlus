// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path/decks.hpp"

#include "park/ground/paths.hpp"
#include "park/ground/queues.hpp"
#include "park/queue_fences.hpp"
#include "park/terrain_mesh.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
#include <unordered_map>

namespace rct3::park::path
{

    namespace
    {

        constexpr std::uint8_t kSideCount = 4;

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
        constexpr std::array<std::array<std::size_t, 2>, kSideCount> kEdgeCorners { { { 2, 3 }, { 1, 3 }, { 0, 1 }, { 0, 2 } } };

        constexpr std::uint8_t rotated_bits(std::uint8_t bits, ModelTurn turn) noexcept
        {
            return static_cast<std::uint8_t>(((bits << turn.value) | (bits >> (kSideCount - turn.value))) & 0b1111);
        }

        bool meets(float a, float b) noexcept
        {
            return std::abs(a - b) <= ground::kPathMeets;
        }

        /// The paths by tile, to find a tile's paths without looking through them all.
        class TilePaths
        {
        public:
            explicit TilePaths(std::span<const save::model::PathTile> paths)
            {
                for (const save::model::PathTile& p : paths)
                    by_tile_[key(p.col, p.row)].push_back(&p);
            }

            std::span<const save::model::PathTile* const> at(int col, int row) const
            {
                if (col < 0 || row < 0)
                    return { };
                const auto found = by_tile_.find(key(col, row));
                return found == by_tile_.end() ? std::span<const save::model::PathTile* const> { } : std::span(found->second);
            }

        private:
            static std::uint32_t key(int col, int row) noexcept
            {
                return static_cast<std::uint32_t>(col) << 16 | static_cast<std::uint32_t>(row);
            }

            std::unordered_map<std::uint32_t, std::vector<const save::model::PathTile*>> by_tile_;
        };

        /// Whether `path` is flat path (not queue) at `height`.
        bool flat_at(const save::model::PathTile& path, const save::model::TerrainGrid& grid, float height)
        {
            if (path.kind == save::model::PathKind::Queue)
                return false;
            if (path.elevation)
                return path.elevation->slope == 0 && meets(static_cast<float>(path.elevation->quantised_height), height);
            if (path.kind != save::model::PathKind::Ground || !grid.contains(path.col, path.row))
                return false;
            const save::model::TerrainCell& cell = grid.at(path.col, path.row);
            return cell.max_height() - cell.min_height() <= kFlat && meets(cell.min_height(), height);
        }

        bool any_flat_at(const TilePaths& tiles, const save::model::TerrainGrid& grid, save::model::Tile tile, float height)
        {
            return std::ranges::any_of(tiles.at(tile.col, tile.row), [&](const save::model::PathTile* p) { return flat_at(*p, grid, height); });
        }

        Deck choose(const save::model::PathTile& path, const TilePaths& tiles, const save::model::TerrainGrid& grid, std::optional<const EdgeWalls&> walls)
        {
            const save::model::PathElevation& e = *path.elevation;
            const auto height = static_cast<float>(e.quantised_height);
            if (e.slope != 0)
            {
                const Side heading = stored_heading_or_bare(path.direction);
                // A gentle slope beside: the same slope, the same way, from the same height.
                const auto beside = [&](Side side) {
                    const auto [col, row] = side.beyond({ path.col, path.row });
                    return std::ranges::any_of(tiles.at(col, row), [&](const save::model::PathTile* p) { return p->kind != save::model::PathKind::Queue && p->elevation && p->elevation->slope == e.slope && p->direction == path.direction && p->elevation->quantised_height == e.quantised_height; });
                };
                return slope_deck(e.slope, heading, beside(heading.left()), beside(heading.right()));
            }
            const save::model::Tile tile { path.col, path.row };
            std::uint8_t sides = 0;
            for (const Side side : kSides)
            {
                const auto [col, row] = side.beyond(tile);
                const Side facing = side.opposite();
                const bool joined = std::ranges::any_of(tiles.at(col, row), [&](const save::model::PathTile* p) {
                    const auto edge = edge_height(*p, grid, facing);
                    return edge && meets(*edge, height) && (p->kind == save::model::PathKind::Queue || !walls || !walled(*walls, tile, side, height));
                });
                if (joined)
                    sides = static_cast<std::uint8_t>(sides | side.bit());
            }
            // Corner k lies between side k and the side after it, so it shares side k's bit.
            std::uint8_t corners = 0;
            for (const Side corner : kSides)
            {
                const Side other = corner.right();
                if ((sides & corner.bit()) == 0 || (sides & other.bit()) == 0)
                    continue;
                const save::model::Tile a = corner.beyond(tile);
                const save::model::Tile b = other.beyond(tile);
                if (any_flat_at(tiles, grid, a, height) && any_flat_at(tiles, grid, b, height) && any_flat_at(tiles, grid, other.beyond(a), height))
                    corners = static_cast<std::uint8_t>(corners | corner.bit());
            }
            return flat_deck(sides, corners);
        }

    } // namespace

    Deck flat_deck(std::uint8_t sides, std::uint8_t corners) noexcept
    {
        sides &= 0b1111;
        // Only corners between two joined sides count.
        corners &= static_cast<std::uint8_t>(sides & rotated_bits(sides, ModelTurn { 3 }));
        for (const Side side : kSides)
        {
            const ModelTurn turn = ModelTurn::toward(side);
            for (const Pattern& p : kPatterns)
            {
                if (rotated_bits(p.sides, turn) == sides && rotated_bits(p.corners, turn) == corners)
                    return { p.piece, turn };
            }
        }
        return { };
    }

    Deck slope_deck(std::uint8_t slope, Side heading, bool before, bool after) noexcept
    {
        const bool up = !slope_falls(slope);
        const ModelTurn turn = ModelTurn::toward(up ? heading.left() : heading.right());
        if (ground::path_rise(slope) > 1)
            return { DeckPiece::Slope, turn };
        // The slope beside this one on the left going up is on the right going down.
        const bool left = up ? after : before;
        const bool right = up ? before : after;
        const DeckPiece piece = left && right ? DeckPiece::SlopeMid : left ? DeckPiece::SlopeStraightLeft : right ? DeckPiece::SlopeStraightRight : DeckPiece::SlopeStraight;
        return { piece, turn };
    }

    namespace
    {
        /// Stored directions by heading, and headings by stored direction: the same swap both ways.
        constexpr std::array<std::uint8_t, kSideCount> kSwap { 3, 1, 2, 0 };
    } // namespace

    std::uint8_t stored_direction(Side heading) noexcept
    {
        return kSwap[static_cast<std::size_t>(heading.value & 3)];
    }

    std::optional<Side> stored_heading(std::uint8_t direction) noexcept
    {
        return direction < kSideCount ? std::optional(Side { kSwap[direction] }) : std::nullopt;
    }

    Side stored_heading_or_bare(std::uint8_t direction) noexcept
    {
        return stored_heading(direction).value_or(Side::from(direction));
    }

    std::array<std::size_t, 2> edge_corners(Side side) noexcept
    {
        return kEdgeCorners[static_cast<std::size_t>(side.value & 3)];
    }

    std::optional<float> edge_height(const save::model::PathTile& path, const save::model::TerrainGrid& grid, Side side)
    {
        if (!grid.contains(path.col, path.row))
            return std::nullopt;
        const auto& corners = grid.at(path.col, path.row).corners;
        const auto [first, second] = edge_corners(side);
        const float ground = (corners[first] + corners[second]) / 2;
        // A queue reaches only the sides its line comes in and goes out by.
        const bool queue_side = path.queue && (stored_heading(path.queue->start_direction) == side || stored_heading(path.queue->end_direction) == side);
        if (ground::is_ground_queue(path))
            return queue_side ? std::optional(ground) : std::nullopt;
        if (!path.elevation)
            return path.kind == save::model::PathKind::Ground ? std::optional(ground) : std::nullopt;
        const save::model::PathElevation& e = *path.elevation;
        const auto height = static_cast<float>(e.quantised_height);
        if (e.slope == 0)
            return path.kind != save::model::PathKind::Queue || queue_side ? std::optional(height) : std::nullopt;
        // Without a stored heading only the end behind its bare count is met, as it always has been.
        if (stored_heading(path.direction) == side)
            return height + (slope_falls(e.slope) ? -ground::path_rise(e.slope) : ground::path_rise(e.slope));
        if (side == stored_heading_or_bare(path.direction).opposite())
            return height;
        return std::nullopt;
    }

    std::string deck_symbol(const PathStyle& style, DeckPiece piece)
    {
        const auto at = static_cast<std::size_t>(piece);
        return at < style.pieces.size() ? style.pieces[at] : std::string();
    }

    std::pair<QueuePiece, ModelTurn> queue_deck(const save::model::PathTile& queue)
    {
        if (!queue.elevation || !queue.queue)
            return { };
        const std::uint8_t slope = queue.elevation->slope;
        if (slope != 0)
        {
            const ModelTurn turn = slope_deck(slope, stored_heading_or_bare(queue.direction), false, false).turn;
            const QueuePiece piece = ground::path_rise(slope) <= 1 ? QueuePiece::SlopeStraight : slope_falls(slope) ? QueuePiece::SlopeDown : QueuePiece::SlopeUp;
            return { piece, turn };
        }
        const Side start = stored_heading_or_bare(queue.queue->start_direction);
        const Side end = stored_heading_or_bare(queue.queue->end_direction);
        const QueuePiece piece = end == start.right() ? QueuePiece::TurnLeft : end == start.left() ? QueuePiece::TurnRight : QueuePiece::Straight;
        return { piece, ModelTurn::toward(start.right()) };
    }

    std::pair<std::string, ModelTurn> deck_for(const PathStyle& style, const save::model::PathTile& path, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid)
    {
        if (path.kind == save::model::PathKind::Queue)
        {
            const auto [piece, turn] = queue_deck(path);
            const auto at = static_cast<std::size_t>(piece);
            return { at < style.pieces.size() ? style.pieces[at] : std::string(), turn };
        }
        const Deck deck = choose_deck(path, paths, grid);
        return { deck_symbol(style, deck.piece), deck.turn };
    }

    Deck choose_deck(const save::model::PathTile& path, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid, std::optional<const EdgeWalls&> walls)
    {
        if (!path.elevation)
            return { };
        return choose(path, TilePaths(paths), grid, walls);
    }

    std::size_t refresh_decks(std::span<const save::model::PathTile> paths, std::vector<save::model::PlacedObject>& objects, const save::model::TerrainGrid& grid, std::span<const PathStyle> styles, int low_col, int low_row, int high_col, int high_row, std::optional<const EdgeWalls&> walls)
    {
        const TilePaths tiles(paths);
        std::unordered_map<std::uint64_t, save::model::PlacedObject*> by_id;
        for (save::model::PlacedObject& o : objects)
            by_id.emplace(o.id, &o);
        std::size_t changed = 0;
        for (int col = low_col; col <= high_col; ++col)
        {
            for (int row = low_row; row <= high_row; ++row)
            {
                for (const save::model::PathTile* p : tiles.at(col, row))
                {
                    const bool queue = p->kind == save::model::PathKind::Queue;
                    if ((p->kind != save::model::PathKind::Flying && !queue) || !p->elevation || ground::is_ground_queue(*p))
                        continue;
                    const auto object = by_id.find(p->elevation->scenery_ref);
                    const auto style = std::ranges::find_if(styles, [&](const PathStyle& s) { return s.queue == queue && strings::equals_ignoring_case(s.name, p->surface_name); });
                    if (object == by_id.end() || style == styles.end())
                        continue;
                    ModelTurn turn;
                    std::string symbol;
                    if (queue)
                        std::tie(symbol, turn) = deck_for(*style, *p, paths, grid);
                    else
                    {
                        const Deck deck = choose(*p, tiles, grid, walls);
                        symbol = deck_symbol(*style, deck.piece);
                        turn = deck.turn;
                    }
                    if (symbol.empty() || (symbol == object->second->symbol && object->second->rotation == turn.value))
                        continue;
                    object->second->symbol = std::move(symbol);
                    object->second->rotation = turn.value;
                    ++changed;
                }
            }
        }
        return changed;
    }

} // namespace rct3::park::path
