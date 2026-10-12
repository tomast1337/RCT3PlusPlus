// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenery item visuals: how a scenery item looks, as a list of levels of detail that each draw a static
// shape, a bone shape or a billboard.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// Values of `SceneryVisualLod::mesh_type` seen in files.
enum class LodMeshType : std::uint32_t {
  StaticShape = 0,
  BoneShape = 3,
  Billboard = 4,
};

struct SceneryVisualLod {
  std::uint32_t mesh_type = 0;  ///< A `LodMeshType` value, other values are kept as read
  std::string_view name;        ///< Name of this level, a string in the file
  std::string_view static_shape;  ///< "shs" item, for static shapes
  std::string_view bone_shape;    ///< "bsh" item, for bone shapes
  std::string_view texture;       ///< "ftx" item, for billboards
  std::string_view style;         ///< "txs" item, for billboards (always the standard billboard style)
  /// Billboard size. 1 x 1 for the other types.
  float width = 0;
  float height = 0;
  /// Texture rectangle as {u0, u1, v0, v1}, {0, 1, 0, 1} for the other types.
  std::array<float, 4> texcoords{};
  /// Distance from which this level is used. Not confirmed, but values grow with the level in all but
  /// a few items.
  float distance = 0;
  std::vector<std::string_view> animations;  ///< "ban" items, for bone shapes
};

struct SceneryVisual {
  std::uint32_t flags = 0;  ///< 0, 1, 5, 0x70, 0x1000000 and 0x2000000 seen. Bit meanings unconfirmed.
  float sway = 0;           ///< 0 for static items, 0.1 to 0.2 for plants
  float brightness = 0;     ///< 1, or 0.6 to 0.8 for plants
  float scale_variation = 0;  ///< 0, or 0.35 to 0.4 for plants
  std::vector<SceneryVisualLod> lods;
};

struct SceneryVisualItem {
  std::string_view name;                 ///< Symbol name without the tag
  std::optional<SceneryVisual> visual;   ///< Nothing when any part of the structure is unreadable
};

/// Scenery item visuals of one part of an overlay: pointer symbols tagged "svd". Views in the result
/// point into the overlay's bytes.
[[nodiscard]] std::vector<SceneryVisualItem> read_scenery_visuals(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
