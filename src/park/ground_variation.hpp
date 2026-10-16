// SPDX-License-Identifier: GPL-3.0-or-later
//
// A tiling picture of smooth noise that the ground is read against in world space, to vary it gently from
// place to place so its repeating pictures do not line up into a visible grid. Each of the four channels holds
// noise of its own, so later effects can vary other things without a picture of their own; for now the
// ground's brightness reads the red one.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rct3::park
{

    /// The picture's width and height in texels.
    inline constexpr std::uint32_t kVariationSize = 128;
    /// Across the picture, how many noise features there are; it tiles without a seam.
    inline constexpr std::uint32_t kVariationCells = 8;

    /// The picture as Rgba8 texels, row by row. Each channel's noise wanders smoothly round mid grey, from 0
    /// to 255, the same every time.
    [[nodiscard]] std::vector<std::byte> variation_texels();

} // namespace rct3::park
