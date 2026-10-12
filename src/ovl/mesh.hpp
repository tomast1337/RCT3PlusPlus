// SPDX-License-Identifier: GPL-3.0-or-later
//
// A neutral in-memory triangle mesh built from the model readers, and a Wavefront OBJ/MTL writer.
//
// The mesh owns its data, so it outlives the overlay it was read from. It is meant for inspecting and
// verifying models outside the project, and as a common input for later renderers.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ovl/bone_shape.hpp"
#include "ovl/static_shape.hpp"

namespace rct3::ovl {

/// A run of triangles that share one texture binding.
struct MeshPart {
  std::string texture;  ///< Flexi texture name, empty when the source part references none
  std::string style;    ///< Texture style name, empty when the source part references none
  /// First entry of `Mesh::indices` used by this part, and how many entries (a multiple of three).
  std::uint32_t first_index = 0;
  std::uint32_t index_count = 0;
  /// The source part's rendering values, copied as read (see the reader headers for what they mean).
  std::uint32_t transparency = 0;
  std::uint32_t flags = 0;
  std::uint32_t sides = 0;
};

/// Triangle list with per-vertex attributes in the model's own coordinate system, unmodified. Texture
/// coordinates are as stored. All parts of a shape share the vertex arrays; indices are global.
struct Mesh {
  std::vector<std::array<float, 3>> positions;
  std::vector<std::array<float, 3>> normals;  ///< Same length as `positions`
  std::vector<std::array<float, 2>> uvs;      ///< Same length as `positions`
  std::vector<std::uint32_t> indices;         ///< Three per triangle, each below `positions.size()`
  std::vector<MeshPart> parts;                ///< Cover `indices` in order without gaps
};

/// Build a mesh from a static shape. Triangles with an index outside their part's vertices are dropped,
/// and a part whose index count is not a multiple of three loses its trailing indices, so the result is
/// always well formed.
[[nodiscard]] Mesh mesh_from_static_shape(const StaticShape& shape);

/// Build a mesh from a bone shape in its rest pose: vertices exactly as stored, with no bone applied.
[[nodiscard]] Mesh mesh_from_bone_shape(const BoneShape& shape);

/// Wavefront OBJ text for a mesh. One group and one `usemtl` per part, 1-based indices.
///
/// The models are stored in a left-handed space (y up, triangles wound clockwise seen from outside). OBJ
/// readers assume a right-handed one, so z is negated for positions and normals and every triangle's
/// second and third corner are swapped; the object then looks the same as in the game, not mirrored,
/// and counter-clockwise triangles face along their normals. The v texture coordinate is flipped
/// (1 - v) because OBJ puts the origin of the image at the bottom left. Non-finite numbers are
/// written as 0. A comment before each `usemtl` line repeats the part's rendering values.
/// `material_library` is the file name written on the `mtllib` line, omitted when empty.
[[nodiscard]] std::string write_obj(const Mesh& mesh, std::string_view material_library);

/// Material library text for the parts of a mesh: one material per distinct texture name, with the
/// texture file named "<texture>.png". Parts without a texture share the material "untextured".
[[nodiscard]] std::string write_mtl(const Mesh& mesh);

/// Material name used by `write_obj` and `write_mtl` for a part: its texture name with characters other
/// than letters, digits, '_', '-' and '.' replaced by '_'.
[[nodiscard]] std::string material_name(const MeshPart& part);

}  // namespace rct3::ovl
