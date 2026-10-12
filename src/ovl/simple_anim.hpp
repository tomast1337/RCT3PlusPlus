// SPDX-License-Identifier: GPL-3.0-or-later
//
// Simple animations: short keyframed motions of a few channels, used for people's heads and similar
// small parts.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// One sample of one channel at one key time: a vector of three floats followed by twelve bytes whose
/// layout is not known. The bytes look like six 16-bit values, some of which sit at the ends of the
/// signed range, so they are probably a packed rotation.
struct SimpleAnimSample {
  std::array<float, 3> vector{};
  std::array<std::int16_t, 6> packed{};
};

struct SimpleAnim {
  std::uint32_t channels = 0;       ///< 2, 3 or 4 in files seen
  std::vector<float> times;         ///< Key times in seconds, one per key
  /// times.size() * channels samples, all channels of the first key, then the second key, and so on.
  std::vector<SimpleAnimSample> samples;
};

struct SimpleAnimItem {
  std::string_view name;           ///< Symbol name without the tag
  std::optional<SimpleAnim> anim;  ///< Nothing when any part of the structure is unreadable
};

/// Simple animations of one part of an overlay: pointer symbols tagged "san".
[[nodiscard]] std::vector<SimpleAnimItem> read_simple_anims(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
