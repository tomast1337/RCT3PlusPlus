// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shapes of objects that move every frame, such as ride cars, as one mesh. Each object's shape is built
// once in its own space; moving them only turns and moves its vertices, which is far cheaper than building
// the mesh again. Every object is drawn at its finest level.
#pragma once

#include "park/object_mesh.hpp"

#include <cstdint>
#include <span>
#include <utility>
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

    /// The objects' shapes, each standing at the origin until moved. The joints of objects on arms (see
    /// ObjectModel::arm_joints) come after all the objects, each object's from the base out, to be moved apart.
    [[nodiscard]] MovingMeshes moving_meshes(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid);

    /// Shapes already built, each in its own space, as one mesh: batches drawn alike are gathered.
    [[nodiscard]] MovingMeshes gathered_meshes(std::span<const ObjectMesh> alone);

    /// The parts of `object` (whose model `model` has a rig) at its rig's level, one piece per bone in the
    /// model's own space, each to be put where its bone is; with the bone of each piece.
    [[nodiscard]] std::pair<MovingMeshes, std::vector<std::int32_t>> boned_meshes(const save::PlacedObject& object, const ObjectModel& model, const save::TerrainGrid& grid);

    /// How each of an arm's joints, at rest at `joints` (model space, from the base out), moves what it carries
    /// once turned by `degrees`: model space, each joint carried by those before it. The first turns about its
    /// own up, the others about their own sideways.
    [[nodiscard]] std::vector<glm::mat4> arm_moves(std::span<const glm::mat4> joints, std::span<const float> degrees);

    /// Puts each object where `places` (model to world, by object) has it, leaving those already there.
    /// Moves and turns only: a stretched place leaves the normals off.
    void move_meshes(MovingMeshes& meshes, std::span<const glm::mat4> places);

} // namespace rct3::park
