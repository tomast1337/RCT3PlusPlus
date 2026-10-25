// SPDX-License-Identifier: GPL-3.0-or-later
//
// Support columns under raised items: coaster track, raised paths and queues, raised rides and anything else
// whose scenery item asks for them. A park does not keep them: they are built when it loads, and again on the
// tiles where something is built, taken away or changed, as placed objects, from what each item asks for
// (support_footprint.hpp) and the pieces of the support set its group names (support_sets.hpp).
//
// One rule for every item, tile by tile. The items on a tile are taken by their undersides, the highest first.
// A square that wants a column gets one under the middle of the tile, from the item's underside down to what is
// next below it on the tile. That is the ground, rounded up to the next whole level from its lowest corner,
// when nothing is below; then a ground path on the tile, or no room between the item and the ground (an item at
// or under the ground, as in a tunnel), leaves the column out. Otherwise it is the next item down: a column
// stands on it only when the set's columns stand on anything (wooden and mine track do) or that item carries
// columns, keeping one level clear over track unless the set stands close; on anything else, such as a path
// deck, a ride, scenery or other track, the tile gets no column. An item buried more than a level under the
// ground counts as the ground. Support pieces, and items the caller names (station platforms), are passed by.
// The column is a foot,
// the set's top piece just under the item, and middles from the tallest down in between, each lined up on
// whole multiples of its height. Items that ask for columns on every other tile get them only where column
// and row differ in evenness.
//
// Items that list support points get a column at the middle of the point's third of a tile instead. Of each
// group of points the first that can have a column gets one. Below a point only items that fill that third
// are in the way, when they say which thirds they fill. When the point lies near the middle of the third the
// column reaches right up to the item; otherwise it stops at the point's column height and the shortest beam
// that spans the gap joins it to the item, stretched to fit. A column stops where it meets a cap, when the item
// asks for one, and its top piece stretches to end there exactly. Such pieces stand off the tile grid, so they
// come with their whole transforms.
#pragma once

#include "park/object_mesh.hpp"
#include "park/support_sets.hpp"
#include "save/model/world.hpp"

#include <optional>
#include <set>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rct3::park
{

    /// The tallest column built, in levels.
    inline constexpr std::int32_t kTallestColumn = 240;

    /// The tile under each square of `object`'s footprint, row by row like the footprint's squares.
    [[nodiscard]] std::vector<std::pair<int, int>> footprint_tiles(const save::PlacedObject& object, const SupportFootprint& footprint);

    using SupportTile = std::pair<int, int>;

    /// What a built piece belongs to.
    struct SupportOf
    {
        std::uint64_t held = 0; ///< The item its column holds up
        SupportTile tile;       ///< The tile its column stands on; a beam may reach over the next
    };

    struct BuiltSupports
    {
        std::vector<save::PlacedObject> pieces;
        std::vector<SupportOf> of; ///< Of each piece
        Fitted fitted;             ///< Of the pieces that stand off the middle of a tile, are stretched, or are beams
    };

    /// The tiles whose columns `object` may change: under its footprint, and where its support points stand
    /// beside it. None when its model is not among `models` or columns pass by it.
    [[nodiscard]] std::vector<SupportTile> support_tiles(const save::PlacedObject& object, const ObjectModels& models);

    /// Placed support pieces under the items among `objects` whose models ask for them, on `tiles` only when
    /// given. Items in `passed` are left out, as if not there. New objects take ids after `last_id`. `models`
    /// must hold the support sets' tops, caps and beams, whose shapes give their sizes.
    [[nodiscard]] BuiltSupports support_columns(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const save::PathTile> paths, const save::TerrainGrid& grid, const SupportSets& sets, std::uint64_t last_id, const std::unordered_set<std::uint64_t>& passed = { }, std::optional<const std::set<SupportTile>&> tiles = std::nullopt);

} // namespace rct3::park
