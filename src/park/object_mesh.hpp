// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's placed objects as one mesh: every visible object's model moved to where it stands, lit by
// the same fixed sun as the ground, and grouped into batches by picture, colours and blending. Batches come
// opaque first, then cut-out, then see-through, which is the order to draw them in.
#pragma once

#include "park/object_models.hpp"
#include "park/terrain_mesh.hpp"
#include "save/model/world.hpp"

#include <map>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    /// A run of `count` indices from `first` painted with one picture in one set of colours, blended one way.
    struct ObjectBatch
    {
        std::string texture; ///< The painted picture's name (see painted_name)
        std::string picture; ///< Key of the picture it is painted from
        FlexiColours colours { };
        PartBlend blend = PartBlend::Opaque;
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };

    struct ObjectMesh
    {
        std::vector<GroundVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<ObjectBatch> batches;
        std::size_t placed = 0;  ///< Objects drawn
        std::size_t missing = 0; ///< Visible objects without a model
    };

    using ObjectModels = std::map<std::string, ObjectModel, std::less<>>;

    /// The key `object`'s model is kept by: its overlay and its symbol.
    [[nodiscard]] std::string model_key(const save::PlacedObject& object);

    [[nodiscard]] ObjectMesh object_mesh(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid);

    /// The painted pictures `batches` draw with, by their names. Batches whose picture is missing get none.
    [[nodiscard]] std::map<std::string, ovl::RgbaImage, std::less<>> paint_batches(std::span<const ObjectBatch> batches, const ModelPictures& pictures);

} // namespace rct3::park
