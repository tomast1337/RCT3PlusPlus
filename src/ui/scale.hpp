// SPDX-License-Identifier: GPL-3.0-or-later
//
// Choosing how big the interface is drawn. The art is pixel art made for screens around 1024 x 768, so
// whole-number scales keep it sharp: a 1080p window draws it at 1, 1440p at 2, 4K at 3.
#pragma once

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    /// Height in pixels that one step of automatic scale covers.
    inline constexpr float kScaleStepHeight = 720;

    /// Whole-number scale for a window `height` pixels tall; at least 1.
    [[nodiscard]] inline float auto_scale(float height)
    {
        return std::max(1.0f, std::floor(height / kScaleStepHeight));
    }

} // namespace rct3::ui
