// SPDX-License-Identifier: GPL-3.0-or-later
//
// How track meets the ground. Each solid square of a piece either stands wholly over its tile's ground, lies
// under at least kGroundCover of it all round, as in a tunnel, or passes into it. Where it passes into it,
// the tile's ground is cut down to the square's floor: the tile's own corners only, so the trench round the
// piece has sharp walls. Cutting leaves the ground as it is when the piece is taken away again.
//
// A piece in a tunnel hollows the ground round each of its squares, up to the square's top, as a tunnel path
// does (path_tunnels.hpp): it opens where the ground beside it lies lower, as at a cut or a cliff, and is
// lined. Nothing in a park's file marks such a piece, so saved parks' tunnels are found the same way.
//
// A cut may not reach what the land tools may not move (ground_holds.hpp), land the park does not own
// outright, or a tile holding water or beside one that does: then the piece may not stand.
#pragma once

#include "park/ground_holds.hpp"
#include "park/object_mesh.hpp"
#include "park/terrain_mesh.hpp"
#include "save/model/world.hpp"

#include <span>
#include <vector>

namespace rct3::park
{

    /// Ground over a solid square's top, in world units, that leaves it wholly under the ground, as a tunnel.
    inline constexpr float kGroundCover = 1; ///< Chosen by eye: a unit of ground holds over a coaster's cars

    /// The cuts the solid squares of `items` (their models loaded) make in the ground, a tile at a time.
    [[nodiscard]] std::vector<GroundCut> ground_cuts(std::span<const save::PlacedObject> items, const ObjectModels& models, const save::TerrainGrid& grid);

    /// The hollows of the pieces of track among `objects` (their models loaded) that lie in tunnels.
    [[nodiscard]] std::vector<GroundOpening> track_hollows(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid);

} // namespace rct3::park
