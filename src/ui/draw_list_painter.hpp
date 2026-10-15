// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paints a DrawList into a picture on the CPU, the way the renderer draws it on the GPU: triangles filled
// where pixel centres fall inside them, textures sampled with linear filtering and clamped at their edges,
// shapes shaded per pixel (shape.hpp), everything blended with straight alpha in painter's order. Slow, but
// needs no graphics device, so tools and tests can look at what the interface draws.
#pragma once

#include "ovl/texture_image.hpp"
#include "ui/draw_list.hpp"

#include <map>

namespace rct3::ui
{

    /// The pictures behind the list's texture ids, rows top to bottom. Id 0, and any id not here, samples
    /// as white, as the renderer's untextured quads do.
    using PainterTextures = std::map<TextureId, ovl::RgbaImage>;

    /// A picture `width` by `height` filled with `background`.
    [[nodiscard]] ovl::RgbaImage blank_picture(std::uint32_t width, std::uint32_t height, Color background);

    /// Draws `list` over `target`. The list's coordinates are the target's pixels.
    void paint_draw_list(const DrawList& list, const PainterTextures& textures, ovl::RgbaImage& target);

} // namespace rct3::ui
