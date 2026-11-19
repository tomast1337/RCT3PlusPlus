// SPDX-License-Identifier: GPL-3.0-or-later
//
// The picture of a track seen from above that a ride's design keeps, for the list of designs to show: the
// line its cars run along, drawn onto a square fitted round it, lighter where the track is higher, and clear
// everywhere else.
#pragma once

#include "core/types.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <span>
#include <vector>

namespace rct3::park::track
{

    /// The side of the square every design the game ships keeps.
    inline constexpr u32 kTopViewSize = 512;

    /// RGBA, `size` by `size`, top row first, x to the right and z down: the line through `points` (a
    /// course's, in metres), grey levels by height, clear where it does not pass. Empty for no points.
    [[nodiscard]] std::vector<u8> top_view(std::span<const glm::vec3> points, u32 size = kTopViewSize);

} // namespace rct3::park::track
