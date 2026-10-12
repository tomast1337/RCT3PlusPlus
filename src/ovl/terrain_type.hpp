// SPDX-License-Identifier: GPL-3.0-or-later
//
// Terrain types: one entry per paintable land surface and per cliff face.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// Structure of 60 bytes. The symbols of a file point into one array of these, in order.
struct TerrainType {
  std::uint32_t unknown_00 = 0;  ///< 1 in every item seen
  std::uint32_t unknown_04 = 0;  ///< 0 in every item seen
  std::uint32_t unknown_08 = 0;  ///< 0 or 1
  std::uint32_t index = 0;       ///< 0 to 31 seen, counting the terrain types in file order
  std::uint32_t unknown_10 = 0;  ///< 0, 1 or 2
  std::string_view texture;      ///< "tex" item
  /// Two strings. Both look like names of text entries, the first for a tooltip and the second for the
  /// label.
  std::string_view help_name;
  std::string_view label_name;
  /// Two packed colours, 0xAARRGGBB or 0xAABBGGRR, which of the two is not confirmed. Usually equal.
  std::array<std::uint32_t, 2> colours{};
  /// Five floats at the end of the structure, -1 to 4 seen. Unconfirmed.
  std::array<float, 5> parameters{};
};

struct TerrainTypeEntry {
  std::string_view name;  ///< Symbol name without the tag
  std::optional<TerrainType> terrain;
};

/// Terrain types of one part of an overlay: pointer symbols tagged "ter". Views point into the overlay.
[[nodiscard]] std::vector<TerrainTypeEntry> read_terrain_types(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
