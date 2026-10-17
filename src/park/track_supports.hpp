// SPDX-License-Identifier: GPL-3.0-or-later
//
// Support columns under coaster track and other raised items. A park does not keep them: they are built
// when it loads, as placed objects, from what each item asks for (support_footprint.hpp) and the pieces of
// the support set its group names (support_sets.hpp).
//
// Our build, tile by tile. The items on a tile are taken from the highest down. A square that wants a column
// gets one under the middle of the tile, from the item's underside down to whatever is below it: the next
// item down when the set's columns may stand on it (one level is kept clear over track, unless the set
// stands close), else the ground, rounded up to the next whole level from its lowest corner. A column that
// would end on an item it may not stand on is left out, and so is one over a ground path or one with no room
// between the item and the ground (an item at or under the ground, as in a tunnel). The column is a foot,
// the set's top piece just under the item, and middles from the tallest down in between. Items that ask for
// columns on every other tile get them only where column and row differ in evenness.
//
// Items that list support points get a column at the middle of the point's third of a tile instead. Of each
// group of points the first that can have a column gets one. When the point lies near the middle of the
// third the column reaches right up to the item; otherwise it stops at the point's column height and the
// shortest beam that spans the gap joins it to the item, stretched to fit. A column stops where it meets a
// cap, when the item asks for one, and its top piece stretches to end there exactly. Such pieces stand off
// the tile grid, so they come with their whole transforms.
#pragma once

#include "park/object_mesh.hpp"
#include "park/support_sets.hpp"
#include "save/model/world.hpp"

#include <span>
#include <utility>
#include <vector>

namespace rct3::park
{

    /// The tallest column built, in levels.
    inline constexpr std::int32_t kTallestColumn = 240;

    /// The tile under each square of `object`'s footprint, row by row like the footprint's squares.
    [[nodiscard]] std::vector<std::pair<int, int>> footprint_tiles(const save::PlacedObject& object, const SupportFootprint& footprint);

    struct BuiltSupports
    {
        std::vector<save::PlacedObject> pieces;
        Fitted fitted; ///< Of the pieces that stand off the middle of a tile, are stretched, or are beams
    };

    /// Placed support pieces under the items among `objects` whose models ask for them. New objects take
    /// ids after `last_id`. `models` must hold the support sets' tops, caps and beams, whose shapes give
    /// their sizes.
    [[nodiscard]] BuiltSupports track_supports(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const save::PathTile> paths, const save::TerrainGrid& grid, const SupportSets& sets, std::uint64_t last_id);

} // namespace rct3::park
