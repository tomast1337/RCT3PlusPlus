// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bone animations: per-bone keyframe tracks for position and rotation over a fixed duration.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// One keyframe of a track: a time in seconds and three values.
struct BoneKeyframe {
  float time = 0;
  /// Translation for a position track. For a rotation track three angles, apparently in radians.
  std::array<float, 3> value{};
};

struct BoneTrack {
  std::string_view name;  ///< Name of the bone the tracks belong to, a string in the file
  std::vector<BoneKeyframe> positions;  ///< Empty when the bone does not move
  std::vector<BoneKeyframe> rotations;  ///< Empty when the bone does not turn
};

struct BoneAnim {
  /// Length of the animation in seconds. Equals the last keyframe time of every item seen.
  float duration = 0;
  std::vector<BoneTrack> bones;
};

struct BoneAnimItem {
  std::string_view name;            ///< Symbol name without the tag
  std::optional<BoneAnim> anim;     ///< Nothing when any part of the structure is unreadable
};

/// Bone animations of one part of an overlay: pointer symbols tagged "ban". Views in the result point
/// into the overlay's bytes.
[[nodiscard]] std::vector<BoneAnimItem> read_bone_anims(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
