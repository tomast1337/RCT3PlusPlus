// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bone shapes: animated 3D models. Meshes whose vertices are weighted to a skeleton of named bones.
#pragma once

#include "core/types.hpp"
#include "ovl/bone_index.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// Bytes of one vertex.
    inline constexpr usize kBoneVertexSize = 44;

    struct BoneVertex
    {
        std::array<f32, 3> position { };
        std::array<f32, 3> normal { }; ///< Unit length in every file seen
        /// Up to four bones that move the vertex, as indices into `BoneShape::bones`. Entries whose weight is
        /// zero are meaningless.
        std::array<u8, 4> bones { };
        /// Weight of each bone out of 255. They add up to 255 in every file seen.
        std::array<u8, 4> weights { };
        std::array<u8, 4> colour { }; ///< Blue, green, red, alpha as in static shapes: opaque white in almost all vertices
        std::array<f32, 2> texcoord { };
    };

    struct BoneMesh
    {
        /// 0xFFFFFFFF in every file seen. Same field as in static shapes.
        u32 support_type = 0;
        std::string_view texture; ///< Flexi texture name without the tag. Empty when not referenced.
        std::string_view style;   ///< Texture style name without the tag. Empty when not referenced.
        u32 transparency = 0;     ///< 0 opaque, 1 alpha tested, 2 alpha blended, as for static shapes
        u32 texture_flags = 0;    ///< Bit mask, same meaning as for static shapes
        u32 sides = 0;            ///< Cull mode: 1 both sides, 3 front only, as for static shapes
        u32 vertex_count = 0;
        u32 index_count = 0;

        /// vertex_count * kBoneVertexSize bytes.
        std::span<const std::byte> vertices;
        /// index_count 16-bit indices into the vertices, three per triangle.
        std::span<const std::byte> indices;

        [[nodiscard]] BoneVertex vertex(usize i) const;
        [[nodiscard]] u16 index(usize i) const;
    };

    struct Bone
    {
        std::string_view name;
        std::optional<BoneIndex> parent { }; ///< None for a root
        /// Row-major 4x4, row vectors (translation in the last row). Relative to the parent bone.
        std::array<f32, 16> local { };
        /// Row-major 4x4 in model space. Equals `local * parent.world`, and `local` itself for a root.
        std::array<f32, 16> world { };
    };

    struct BoneShape
    {
        std::array<f32, 3> bounds_min { };
        std::array<f32, 3> bounds_max { };
        u32 vertex_total = 0; ///< Sum of the meshes' vertex counts
        u32 index_total = 0;  ///< Sum of the meshes' index counts
        std::vector<BoneMesh> meshes;
        std::vector<Bone> bones;
    };

    struct BoneShapeItem
    {
        std::string_view name;          ///< Symbol name without the tag
        std::optional<BoneShape> shape; ///< Nothing when any part of the structure is unreadable
    };

    /// Bone shapes of one part of an overlay: pointer symbols tagged "bsh". Views and spans in the result
    /// point into the overlay's bytes.
    [[nodiscard]] std::vector<BoneShapeItem> read_bone_shapes(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
