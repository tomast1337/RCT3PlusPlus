// SPDX-License-Identifier: GPL-3.0-or-later
//
// How track meets the ground. Each solid square of a piece either stands wholly over its tile's ground, lies
// under at least kGroundCover of it all round, as in a tunnel, or passes into it. Where it passes into it,
// the tile's ground is cut down to the square's floor: the tile's own corners only, so the trench round the
// piece has sharp walls. Cutting leaves the ground as it is when the piece is taken away again.
//
// A cut may not reach what the land tools may not move (ground_holds.hpp), land the park does not own
// outright, or a tile holding water or beside one that does: then the piece may not stand.
#pragma once

#include "park/ground_holds.hpp"
#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <array>
#include <span>
#include <vector>

namespace rct3::park
{

    /// Ground over a solid square's top, in world units, that leaves it wholly under the ground, as a tunnel.
    inline constexpr float kGroundCover = 1; ///< Chosen by eye: a unit of ground holds over a coaster's cars

    /// A tile's ground after a cut.
    struct GroundCut
    {
        save::Tile tile { };
        std::array<float, 4> corners { };
    };

    /// The cuts the solid squares of `items` (their models loaded) make in the ground, a tile at a time.
    [[nodiscard]] std::vector<GroundCut> ground_cuts(std::span<const save::PlacedObject> items, const ObjectModels& models, const save::TerrainGrid& grid);

    /// Whether any of `cuts` may not be made, for `holds`, ownership or the water of `bodies`.
    [[nodiscard]] bool cuts_refused(std::span<const GroundCut> cuts, const save::TerrainGrid& grid, const GroundHolds& holds, std::span<const save::WaterBody> bodies);

} // namespace rct3::park
