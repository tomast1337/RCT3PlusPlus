// SPDX-License-Identifier: GPL-3.0-or-later
//
// Model animations: per-frame bone positions and rotations for a skinned model at a fixed frame rate.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

struct ModelAnim {
  float duration = 0;               ///< Seconds. Equals (frame_count - 1) / 30 in every item seen.
  std::uint32_t frame_count = 0;
  /// Bones that move. Names are strings in the item's extra data chunk (views into the part's bytes).
  std::vector<std::string_view> position_bones;
  /// Bones that turn. In files seen this lists every bone of the model.
  std::vector<std::string_view> rotation_bones;
  /// frame_count * position_bones.size() translations, all bones of the first frame, then the next.
  std::vector<std::array<float, 3>> positions;
  /// frame_count * rotation_bones.size() rotations in the same order. Four floats of unit length, so
  /// most likely a quaternion. The component order is not confirmed.
  std::vector<std::array<float, 4>> rotations;
};

struct ModelAnimItem {
  std::string_view name;            ///< Symbol name without the tag, empty when no symbol matches
  std::optional<ModelAnim> anim;    ///< Nothing when any part of the structure is unreadable
};

/// Model animations of one part of an overlay: loader rows of the "modelanim" loader. The item header
/// has two more name lists (a plain list and a list of pairs) that are empty in every file seen. An item
/// where either is not empty is reported as unreadable.
[[nodiscard]] std::vector<ModelAnimItem> read_model_anims(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
