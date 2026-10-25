// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal models: the skinned meshes of zoo animals, each with its skeleton and several levels of detail.
//
// The item itself is a small header; everything else lives in two extra data chunks. The first holds the
// skeleton, the material names, the levels of detail and their meshes, laid out back to back with some
// sections starting on 16-byte boundaries (counted from the chunk's start). The second holds the bones'
// names, one after another, each ended by a NUL.
//
// Vertices are stored in model space, in the resting pose, with the same 44-byte layout as bone shapes
// (see bone_shape.hpp). Bones are stored relative to their parent, as an offset and a turn.
#pragma once

#include "ovl/bone_shape.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct AnimalBone
    {
        std::string_view name;
        std::int32_t parent = -1;                     ///< Index of an earlier bone, or -1 for a root
        std::array<float, 3> position { };            ///< At rest, from the parent bone
        std::array<float, 4> rotation { 0, 0, 0, 1 }; ///< At rest, from the parent bone: a unit quaternion x, y, z, w
    };

    struct AnimalModel
    {
        std::array<float, 3> bounds_min { };
        std::array<float, 3> bounds_max { };
        std::vector<AnimalBone> bones;
        /// Each level of detail, finest first, as meshes. A mesh's `texture` is the name of its material,
        /// which names a texture ("tex" item) without the tag. Coarse levels may have no meshes.
        std::vector<std::vector<BoneMesh>> levels;
    };

    struct AnimalModelItem
    {
        std::string_view name;            ///< Symbol name without the tag, empty when no symbol matches
        std::optional<AnimalModel> model; ///< Nothing when any part of the structure is unreadable or unsupported
    };

    /// Animal models of one part of an overlay: loader rows of the "mdl" loader. Views and spans in the result
    /// point into the overlay's bytes.
    [[nodiscard]] std::vector<AnimalModelItem> read_animal_models(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
