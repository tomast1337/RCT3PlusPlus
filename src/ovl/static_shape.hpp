// SPDX-License-Identifier: GPL-3.0-or-later
//
// Static shapes: fixed 3D models made of meshes, plus named attachment points (effects).
#pragma once

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

    /// Bytes of one vertex: position, normal, a packed colour and a texture coordinate pair.
    inline constexpr std::size_t kShapeVertexSize = 36;

    struct ShapeVertex
    {
        std::array<float, 3> position { };
        std::array<float, 3> normal { }; ///< Unit length in every file seen
        /// Vertex colour, four bytes in file order (blue, green, red, alpha). Alpha is 255 in every file seen
        /// and almost all vertices are white.
        std::array<std::uint8_t, 4> colour { };
        std::array<float, 2> texcoord { }; ///< u, v. Origin at the top left of the image, values beyond 0..1 occur
    };

    /// Confirmed bits of `ShapeMesh::texture_flags` (shared with bone shapes). With the terrain bit set, the
    /// surface or cliff bit chooses which terrain texture the engine draws instead of the part's own.
    inline constexpr std::uint32_t kMeshFlagTerrainTexture = 0x1000;
    inline constexpr std::uint32_t kMeshFlagTerrainSurface = 0x2000;
    inline constexpr std::uint32_t kMeshFlagTerrainCliff = 0x4000;
    /// The part shows an advertisement texture chosen at run time.
    inline constexpr std::uint32_t kMeshFlagAdvertisement = 0x8000;
    /// A sign's or entrance's board, where words are shown. On queue signs it is the only such part, drawn
    /// unlit, its texture coordinates running along the board.
    inline constexpr std::uint32_t kMeshFlagBoard = 0x8;

    struct ShapeMesh
    {
        /// 0xFFFFFFFF for an ordinary part. Parts of path, queue and track pieces that belong to one kind of
        /// support carry a single bit (1, 2, 4 or 8) instead. Used only by those piece types; the exact
        /// selection rule is not confirmed.
        std::uint32_t support_type = 0;
        std::string_view texture; ///< Flexi texture name without the tag. Empty when not referenced.
        std::string_view style;   ///< Texture style name without the tag. Empty when not referenced.
        /// How the part is blended: 0 opaque, 1 alpha tested (cut-out), 2 alpha blended (drawn in a later,
        /// sorted pass). Always agrees with the texture style's name in files seen.
        std::uint32_t transparency = 0;
        /// Bit mask that selects special handling, 0 for almost all parts. Terrain and advertisement bits are
        /// the `kMeshFlag*` constants. The low bits are not fully understood: 0x4 is present in every nonzero
        /// value below 0x1000. With 0x8 it marks sign and entrance boards, which are drawn with the texture
        /// style alone. With 0x10 it marks chain lifts and flowing water, with 0x20 or 0x40 wheels, whose
        /// texture is likely animated at run time. Bit 0x1 is never stored.
        std::uint32_t texture_flags = 0;
        /// Face culling, a Direct3D cull mode: 1 draws both sides of every triangle, 3 draws only the front
        /// (triangles wound clockwise as seen from outside in the game's left-handed space).
        std::uint32_t sides = 0;
        std::uint32_t vertex_count = 0;
        std::uint32_t index_count = 0;

        /// vertex_count * kShapeVertexSize bytes.
        std::span<const std::byte> vertices;
        /// index_count 32-bit indices into the vertices, three per triangle. Some meshes store further
        /// per-index data after these, which is not exposed.
        std::span<const std::byte> indices;

        [[nodiscard]] ShapeVertex vertex(std::size_t i) const;
        [[nodiscard]] std::uint32_t index(std::size_t i) const;
    };

    /// A named attachment point: a row-major 4x4 transform.
    struct ShapeEffect
    {
        std::string_view name;
        std::array<float, 16> transform { };
    };

    struct StaticShape
    {
        std::array<float, 3> bounds_min { };
        std::array<float, 3> bounds_max { };
        std::uint32_t vertex_total = 0; ///< Sum of the meshes' vertex counts
        std::uint32_t index_total = 0;  ///< Sum of the meshes' index counts
        /// Stored next to the mesh count and never larger than it. Purpose unknown.
        std::uint32_t secondary_count = 0;
        std::vector<ShapeMesh> meshes;
        std::vector<ShapeEffect> effects;
    };

    struct StaticShapeItem
    {
        std::string_view name;            ///< Symbol name without the tag
        std::optional<StaticShape> shape; ///< Nothing when any part of the structure is unreadable
    };

    /// Static shapes of one part of an overlay: pointer symbols tagged "shs". Views and spans in the result
    /// point into the overlay's bytes.
    [[nodiscard]] std::vector<StaticShapeItem> read_static_shapes(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
