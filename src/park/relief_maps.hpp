// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's pictures are colour only. A relief map is made from one for the Surface detail option: the
// picture's light and dark read as height, with its broad shading taken out and its fine detail stretched to a
// common strength, so faint pictures (grass, dirt) get as much relief as busy ones (brick, rock). The lit
// shaders bump the surface by it and, at the highest level, shift the picture by its depth.
#pragma once

#include "ovl/texture_image.hpp"

#include <map>
#include <string>
#include <string_view>

namespace rct3::park
{

    /// Added to a picture's name for the name of its relief map.
    inline constexpr std::string_view kReliefSuffix = "#relief";

    /// The relief map of `picture`, the same size, Rgba8: red and green the surface's lean along the picture's x
    /// and y (128 flat), blue the height (128 the middle), alpha how open to the sky the point is (255 open,
    /// lower in hollows such as the mortar between bricks). It wraps at the edges, as tiling pictures do. A
    /// picture with no detail gives a flat, open map. `strength` scales the height round its middle, so 0.5
    /// gives half as deep a relief.
    [[nodiscard]] ovl::RgbaImage relief_map(const ovl::RgbaImage& picture, float strength = 1.0f);

    /// Adds the relief map of every picture in `pictures` under its name and kReliefSuffix.
    void add_relief_maps(std::map<std::string, ovl::RgbaImage, std::less<>>& pictures, float strength = 1.0f);

} // namespace rct3::park
