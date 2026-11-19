// SPDX-License-Identifier: GPL-3.0-or-later
//
// Choosing how big the interface is drawn. The art is pixel art made for screens around 1024 x 768, so
// whole-number scales keep it sharp: a 1080p window draws it at 1, 1440p at 2, 4K at 3. The user may
// choose a scale instead (interface_scale in the settings file).
#pragma once

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace rct3::ui
{

    /// Height in pixels that one step of automatic scale covers.
    inline constexpr f32 kScaleStepHeight = 720;

    /// Whole-number scale for a window `height` pixels tall; at least 1.
    [[nodiscard]] inline f32 auto_scale(f32 height)
    {
        return std::max(1.0f, std::floor(height / kScaleStepHeight));
    }

    /// The scale the user chose, else the automatic one for a screen `height` pixels tall, drawn at `render`
    /// times the window's pixels: the interface keeps its size in the window whatever the render scale.
    [[nodiscard]] inline f32 interface_scale(f32 height, std::optional<f32> chosen, f32 render = 1)
    {
        return chosen.value_or(auto_scale(height / render)) * render;
    }

} // namespace rct3::ui
