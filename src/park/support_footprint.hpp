// SPDX-License-Identifier: GPL-3.0-or-later
//
// What an item asks of the support columns under it, as its scenery item tells: track pieces and other
// raised items carry a support group (the set of pieces their columns are built from) and, per square of
// their footprint, how low the item reaches and which way a column under that square faces.
//
// Two ways of asking exist. Most items want one column under the middle of each square that asks for one.
// Others, many steel coasters' pieces, list support points instead: each names a spot on the item where a
// column meets it, the spot on the ground the column stands at (on a third of a tile, possibly beside the
// item), and how high the column reaches there; a beam joins the two spots when they differ. Points come in
// groups of alternatives, of which one is built.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rct3::ovl
{
    struct SceneryItem;
}

namespace rct3::park
{

    /// Which way a column faces under a square: none, any way, one of four diagonal turns, or along either
    /// axis, numbered as scenery items store it.
    using SupportKind = std::uint32_t;
    inline constexpr SupportKind kNoSupport = 0;
    inline constexpr SupportKind kRoundSupport = 1;
    inline constexpr SupportKind kFirstDiagonal = 2; ///< 2 to 5, a quarter turn apart
    inline constexpr SupportKind kAlongX = 6;
    inline constexpr SupportKind kAlongZ = 7;

    /// `kind` after the item it is under turns by `quarter_turns`.
    [[nodiscard]] SupportKind turned(SupportKind kind, std::int32_t quarter_turns);

    struct SupportSquare
    {
        std::int32_t low = 0;  ///< Lowest level the item takes on this square, from its base
        std::int32_t high = 0; ///< Highest
        SupportKind kind = kNoSupport;
        bool wants_column = false;
        std::uint32_t cells = 0; ///< Bit 3 * z + x: the thirds of the square, across and along, the item fills
    };

    /// A spot on an item, as a square of its footprint, a third of that square and a place in that third.
    struct SupportSpot
    {
        std::array<std::int32_t, 2> square { }; ///< May lie outside the footprint, for a column beside it
        std::array<std::int32_t, 2> cell { };   ///< 0 to 2 across and along
        std::array<float, 2> within { 0.5f, 0.5f };
    };

    struct SupportPoint
    {
        SupportSpot top;            ///< Where the column meets the item
        float top_height = 0;       ///< Levels above the item's base
        SupportSpot column;         ///< Where it stands, at the middle of the third
        float column_height = 0;    ///< How high it reaches when a beam joins it to the item
        bool hanging = false;       ///< The item hangs below the beam, as inverted coasters do
        bool straight_only = false; ///< Built only when the column can stand right under the spot
        std::uint32_t group = 0;    ///< Points of one group are alternatives
    };

    struct SupportFootprint
    {
        std::int32_t x_squares = 0;
        std::int32_t z_squares = 0;
        std::vector<SupportSquare> squares; ///< Row by row along z
        std::vector<SupportPoint> points;   ///< Empty for items that want a column per square
        bool wants_supports = false;
        bool by_points = false;
        bool alternate_tiles = false; ///< Only on every other tile, as on a chequerboard
        bool capped = false;          ///< A cap goes on top of point columns
        bool carries_columns = false; ///< Columns of items above may stand on it
        bool track = false;           ///< A piece of ride track; columns standing on it keep a level clear
    };

    /// What `item` asks for, or offers to columns from above; nothing when columns pass by it, as they do by
    /// items with no footprint and those flagged for it.
    [[nodiscard]] std::optional<SupportFootprint> support_footprint(const ovl::SceneryItem& item);

} // namespace rct3::park
