// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supports under raised paths. They are not kept in a park: they are built when it loads, as placed objects
// standing on the ground under each raised path piece. Each path style has a set of support pieces, named
// by a group that the style's path pieces also carry: a foot that stands on the ground, another foot that
// reaches a little into it for ground that is not level, and middle pieces of a few heights to stack.
//
// Our stack: the foot on the ground, then middles from tallest to shortest, as many of each as still fit,
// up to the underside of the path piece. A tile whose ground already has a path, or whose piece is too near
// the ground for a foot, gets none; where two raised pieces share a tile only the lower one is held up.
#pragma once

#include "park/object_mesh.hpp"
#include "park/support_sets.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    /// One piece of a stack and the level its base stands at.
    struct StackedPiece
    {
        std::string symbol;
        std::int32_t level = 0;
    };

    /// The pieces holding up something whose underside is at level `top`, over ground whose tile corners
    /// go from `ground_low` to `ground_high`. Empty when the gap is too small for a foot.
    [[nodiscard]] std::vector<StackedPiece> support_stack(const SupportSet& set, float ground_low, float ground_high, std::int32_t top);

    /// Placed supports under the raised paths among `paths`, whose pieces are among `objects` with their
    /// models in `models`. New objects take ids after the largest in `objects`.
    [[nodiscard]] std::vector<save::PlacedObject> path_supports(std::span<const save::PathTile> paths, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const SupportSets& sets);

} // namespace rct3::park
