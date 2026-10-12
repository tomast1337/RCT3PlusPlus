// SPDX-License-Identifier: GPL-3.0-or-later
//
// Static shapes: fixed 3D models made of meshes, plus named attachment points (effects).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// Bytes of one vertex: nine 32-bit floats.
inline constexpr std::size_t kShapeVertexSize = 36;

struct ShapeVertex {
  std::array<float, 3> position{};
  std::array<float, 3> normal{};  ///< Unit length in every file seen
  std::array<float, 2> texcoord{};
  float extra = 0;  ///< Ninth float. Purpose unknown, often 0 or 1.
};

struct ShapeMesh {
  /// Small bit-like values 1, 2, 4, 8 or 0xFFFFFFFF (none) in files seen. Meaning not confirmed.
  std::uint32_t support_type = 0;
  std::string_view texture;  ///< Flexi texture name without the tag. Empty when not referenced.
  std::string_view style;    ///< Texture style name without the tag. Empty when not referenced.
  std::uint32_t transparency = 0;   ///< 0, 1 or 2 in files seen. Meaning not confirmed.
  std::uint32_t texture_flags = 0;  ///< Bit mask. Meaning not confirmed.
  std::uint32_t sides = 0;          ///< 1 or 3 in files seen. Meaning not confirmed.
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
struct ShapeEffect {
  std::string_view name;
  std::array<float, 16> transform{};
};

struct StaticShape {
  std::array<float, 3> bounds_min{};
  std::array<float, 3> bounds_max{};
  std::uint32_t vertex_total = 0;  ///< Sum of the meshes' vertex counts
  std::uint32_t index_total = 0;   ///< Sum of the meshes' index counts
  /// Stored next to the mesh count and never larger than it. Purpose unknown.
  std::uint32_t secondary_count = 0;
  std::vector<ShapeMesh> meshes;
  std::vector<ShapeEffect> effects;
};

struct StaticShapeItem {
  std::string_view name;               ///< Symbol name without the tag
  std::optional<StaticShape> shape;    ///< Nothing when any part of the structure is unreadable
};

/// Static shapes of one part of an overlay: pointer symbols tagged "shs". Views and spans in the result
/// point into the overlay's bytes.
[[nodiscard]] std::vector<StaticShapeItem> read_static_shapes(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
