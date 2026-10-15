// SPDX-License-Identifier: GPL-3.0-or-later
//
// Simple animations: short keyframed motions of a few channels, used for people's heads and similar
// small parts.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// One sample of one channel at one key time: where the channel's bone sits, then its turn as the first
    /// two of its three axes, three signed 16-bit values each with 32767 standing for 1. The third axis is
    /// the cross product of the first two. Position and axes are relative to the bone it hangs from.
    struct SimpleAnimSample
    {
        std::array<float, 3> vector { };
        std::array<std::int16_t, 6> packed { };
    };

    struct SimpleAnim
    {
        std::uint32_t channels = 0; ///< 2, 3 or 4 in files seen
        std::vector<float> times;   ///< Key times in seconds, one per key
        /// times.size() * channels samples, all channels of the first key, then the second key, and so on.
        std::vector<SimpleAnimSample> samples;
    };

    struct SimpleAnimItem
    {
        std::string_view name;          ///< Symbol name without the tag
        std::optional<SimpleAnim> anim; ///< Nothing when any part of the structure is unreadable
    };

    /// Simple animations of one part of an overlay: pointer symbols tagged "san".
    [[nodiscard]] std::vector<SimpleAnimItem> read_simple_anims(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
