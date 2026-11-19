// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shapes of objects that move every frame, such as ride cars, as one mesh. Each object's shape is built
// once in its own space; moving them only turns and moves its vertices, which is far cheaper than building
// the mesh again. Every object is drawn at its finest level.
#pragma once

#include "core/types.hpp"
#include "park/object/mesh.hpp"
#include "units/units.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace rct3::park
{

    /// The bones that move one vertex of a model moved by bones, as places in MovingMeshes::places, and how much
    /// each does, out of 255.
    struct VertexSkin
    {
        std::array<u8, 4> bones { };
        std::array<u8, 4> weights { };
    };

    struct MovingMeshes
    {
        object::ObjectMesh mesh;       ///< All the objects where they were last put
        std::vector<GroundVertex> own; ///< The vertices of `mesh` in their objects' own spaces
        std::vector<u32> ends;         ///< By object: one past its last vertex in `mesh`
        std::vector<glm::mat4> places; ///< By object, or by bone when skinned: where it was last put
        /// Of a model moved by bones: by vertex of `own`, the bones that move it. Empty when each object moves
        /// whole.
        std::vector<VertexSkin> skins;
    };

    /// The objects' shapes, each standing at the origin until moved. The joints of objects on arms (see
    /// ObjectModel::arm_joints) and cars' parts drawn apart (ObjectModel::car_part) come after all the objects,
    /// each object's from the base out, to be moved apart.
    [[nodiscard]] MovingMeshes moving_meshes(std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models, const save::model::TerrainGrid& grid);

    /// Shapes already built, each in its own space, as one mesh: batches drawn alike are gathered.
    [[nodiscard]] MovingMeshes gathered_meshes(std::span<const object::ObjectMesh> alone);

    /// The parts of a rigged object, skinned to the bones that move them.
    struct BonedMeshes
    {
        MovingMeshes meshes { };    ///< In the model's own space, each vertex skinned to the bones that move it
        std::vector<i32> bones { }; ///< The rig's bones, one per place to be given
    };

    /// The parts of `object` (whose model `model` has a rig) at its rig's level.
    [[nodiscard]] BonedMeshes boned_meshes(const save::model::PlacedObject& object, const object::ObjectModel& model, const save::model::TerrainGrid& grid);

    /// How each of an arm's joints, at rest at `joints` (model space, from the base out), moves what it carries
    /// once turned by `degrees`: model space, each joint carried by those before it. The first turns about its
    /// own up, the others about their own sideways.
    [[nodiscard]] std::vector<glm::mat4> arm_moves(std::span<const glm::mat4> joints, std::span<const f32> degrees);

    /// How a car's moving part moves what it carries, model space, with the car placed at `place` (model to world):
    /// turned about its pivot's line as far as brings its up nearest the world's (+Y), then on by `swung`, as
    /// glm::rotate turns about that line. A part sprung to its car (CarPart::moves' spring) turns from where it sits
    /// on it by `swung` alone, and one that spins turns about its car's up by `swung` alone. Its heading follows the
    /// car's.
    [[nodiscard]] glm::mat4 part_move(const object::CarPart& part, const glm::mat4& place, units::Radians swung = { });

    /// Puts each object where `places` (model to world, by object) has it, leaving those already there.
    /// Moves and turns only: a stretched place leaves the normals off. Skinned meshes take a place by bone
    /// instead, and each vertex goes where its bones' places, blended by their weights, put it.
    void move_meshes(MovingMeshes& meshes, std::span<const glm::mat4> places);

} // namespace rct3::park
