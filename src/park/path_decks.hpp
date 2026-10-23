// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pieces raised paths are drawn with, and which one each tile takes.
//
// A raised path tile is drawn by one piece of its style, turned by quarter turns. A style names nineteen
// pieces in its description, in the order of DeckPiece. Sides and corners are numbered as the path
// builder's headings: side 0 towards higher rows, 1 higher columns, 2 lower rows, 3 lower columns, and
// corner k between side k and side k + 1. Turning a piece by r quarters moves each of its sides and
// corners r places on.
//
// A flat tile's piece follows the sides it joins a neighbouring path by, and its filled corners: a corner
// between two joined sides is filled when the tile across it has flat path at the same height, so wide paths
// and plazas lose the kerbs inside them. Unturned, the pieces are open and filled as follows:
//
//     piece                                  open sides    filled corners
//     Flat                                   none          -
//     TurnU (a dead end)                     1             -
//     StraightA                              1 3           -
//     TurnLA, TurnLB                         1 2           none, 1
//     TurnTA, TurnTB, TurnTC, StraightB      0 1 3         none, 3, 0, both 0 and 3
//     TurnX, CornerD, CornerB, CornerC       all           none, 1, 0 and 3, 1 and 3
//     CornerA, Flat                          all           0 2 3, all
//
// Where several turns fit (a straight piece fits two), the smallest is taken. This matches the pieces parks
// store for their raised paths.
//
// A sloping tile takes the slope piece of its steepness. A gentle slope that has a gentle slope beside it,
// climbing the same way from the same height, takes the wide pieces instead: the middle one with slopes on
// both sides, else the left or right edge, as the parks store them.
//
// A raised queue tile's piece follows its own line, not its neighbours. Level, it is the straight piece when its
// start and end sides face apart, else a turn: the left turn when the end side is a quarter turn clockwise
// from the start side, the right turn otherwise; the piece is turned one more quarter than its start side.
// Sloping, it is the up, down or gentle slope piece, turned as a path's slope is. This matches the queues the
// parks store.
#pragma once

#include "park/edge_walls.hpp"
#include "park/path_styles.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rct3::park
{

    enum class DeckPiece : std::uint8_t
    {
        Flat,
        StraightA,
        StraightB, ///< The edge of a wide path
        TurnU,
        TurnLA,
        TurnLB, ///< With its inside corner filled
        TurnTA,
        TurnTB,
        TurnTC,
        TurnX,
        CornerA,
        CornerB,
        CornerC,
        CornerD,
        Slope, ///< Steep
        SlopeStraight,
        SlopeStraightLeft,
        SlopeStraightRight,
        SlopeMid,
    };
    inline constexpr std::size_t kDeckPieceCount = 19;

    /// A queue style's pieces, in the order its description names them.
    enum class QueuePiece : std::uint8_t
    {
        Straight,
        TurnLeft,
        TurnRight,
        SlopeUp,
        SlopeDown,
        SlopeStraight, ///< Gentle, either way
    };

    struct Deck
    {
        DeckPiece piece = DeckPiece::Flat;
        std::uint8_t turns = 0; ///< Quarter turns, as the piece's object stores its rotation
        friend bool operator==(const Deck&, const Deck&) = default;
    };

    /// The piece of a flat tile joined on the sides set in `sides` (bit k for side k) with the corners set in
    /// `corners` filled. Corners whose two sides are not both joined are ignored.
    [[nodiscard]] Deck flat_deck(std::uint8_t sides, std::uint8_t corners) noexcept;

    /// The piece of a tile of stored slope `slope` (1 or 2 steep, 4 or 5 gentle) entered heading `heading`,
    /// with a gentle slope beside it on side heading + 3 (`before`) or heading + 1 (`after`).
    [[nodiscard]] Deck slope_deck(std::uint8_t slope, std::uint8_t heading, bool before, bool after) noexcept;

    /// A heading as a park stores a path's direction (0 towards lower columns, 1 higher columns, 2 lower
    /// rows, 3 higher rows), and back: the same swap both ways.
    [[nodiscard]] std::uint8_t stored_direction(std::uint8_t heading) noexcept;

    /// The two corners of a tile (in the terrain's corner order) on its edge on `side`.
    [[nodiscard]] std::array<std::size_t, 2> edge_corners(std::uint8_t side) noexcept;

    /// The height at which `path` on its tile of `grid` meets the tile's edge on `side`; nothing when it does not
    /// reach that edge.
    [[nodiscard]] std::optional<float> edge_height(const save::PathTile& path, const save::TerrainGrid& grid, std::uint8_t side);

    /// The deck piece of a raised path of `style` on a tile.
    [[nodiscard]] std::string deck_symbol(const PathStyle& style, DeckPiece piece);

    /// The piece of a raised queue tile and its turns (Deck::piece is left Flat).
    [[nodiscard]] std::pair<QueuePiece, std::uint8_t> queue_deck(const save::PathTile& queue);

    /// The symbol and turns of the object drawing raised path or queue `path` of `style` among `paths`; an empty
    /// symbol when the style lacks the piece.
    [[nodiscard]] std::pair<std::string, std::uint8_t> deck_for(const PathStyle& style, const save::PathTile& path, std::span<const save::PathTile> paths, const save::TerrainGrid& grid);

    /// Gives each raised path and queue on the tiles from `low` to `high` (inclusive) the piece and turn
    /// its neighbours call for, changing the symbol and rotation of the object that draws it. The pieces come
    /// from the path's style among `styles`. A path does not join one across a wall of `walls`. Returns how many
    /// objects changed.
    std::size_t refresh_decks(std::span<const save::PathTile> paths, std::vector<save::PlacedObject>& objects, const save::TerrainGrid& grid, std::span<const PathStyle> styles, int low_col, int low_row, int high_col, int high_row, std::optional<const EdgeWalls&> walls = std::nullopt);

    /// The piece a raised path should have now, given the other paths and the walls between them.
    [[nodiscard]] Deck choose_deck(const save::PathTile& path, std::span<const save::PathTile> paths, const save::TerrainGrid& grid, std::optional<const EdgeWalls&> walls = std::nullopt);

} // namespace rct3::park
