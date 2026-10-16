// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shapes of objects that move every frame, such as ride cars, as one mesh. Each object's shape is built
// once in its own space; moving them only turns and moves its vertices, which is far cheaper than building
// the mesh again. Every object is drawn at its finest level.
#pragma once

#include "park/object_mesh.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace rct3::park
{

    struct MovingMeshes
    {
        ObjectMesh mesh;                 ///< All the objects where they were last put
        std::vector<GroundVertex> own;   ///< The vertices of `mesh` in their objects' own spaces
        std::vector<std::uint32_t> ends; ///< By object: one past its last vertex in `mesh`
        std::vector<glm::mat4> places;   ///< By object: where it was last put
    };

    /// The objects' shapes, each standing at the origin until moved.
    [[nodiscard]] MovingMeshes moving_meshes(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid);

    /// Puts each object where `places` (model to world, by object) has it, leaving those already there.
    /// Moves and turns only: a stretched place leaves the normals off.
    void move_meshes(MovingMeshes& meshes, std::span<const glm::mat4> places);

} // namespace rct3::park
