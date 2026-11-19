// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track/top_view.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/common.hpp>

namespace rct3::park::track
{

    namespace
    {
        constexpr f32 kMargin = 8;        ///< Chosen by eye: pixels kept clear round the line
        constexpr u8 kLowest = 96;        ///< Chosen by eye: the level of the lowest track, still plain on black
        constexpr f32 kStepsPerPixel = 2; ///< Dots along each stretch, so the line has no gaps
    } // namespace

    std::vector<u8> top_view(std::span<const glm::vec3> points, u32 size)
    {
        if (points.empty() || size == 0)
            return { };
        glm::vec3 low = points.front(), high = points.front();
        for (const glm::vec3& p : points)
        {
            low = glm::min(low, p);
            high = glm::max(high, p);
        }
        const f32 room = std::max(static_cast<f32>(size) - 2 * kMargin, 1.0f);
        const f32 scale = room / std::max({ high.x - low.x, high.z - low.z, 1e-3f });
        // Centred both ways.
        const f32 left = kMargin + (room - (high.x - low.x) * scale) / 2;
        const f32 top = kMargin + (room - (high.z - low.z) * scale) / 2;
        const f32 rise = high.y - low.y;

        std::vector<u8> rgba(usize { size } * size * 4, 0);
        const auto dot = [&](glm::vec3 p) {
            const auto x = static_cast<i64>(std::floor(left + (p.x - low.x) * scale));
            const auto y = static_cast<i64>(std::floor(top + (p.z - low.z) * scale));
            if (x < 0 || y < 0 || x >= size || y >= size)
                return;
            const auto level = static_cast<u8>(kLowest + std::lround((255 - kLowest) * (rise > 0 ? (p.y - low.y) / rise : 1.0f)));
            const usize at = (static_cast<usize>(y) * size + static_cast<usize>(x)) * 4;
            // Where the track passes over itself, the higher stretch shows.
            if (rgba[at + 3] != 0 && rgba[at] >= level)
                return;
            rgba[at] = rgba[at + 1] = rgba[at + 2] = level;
            rgba[at + 3] = 255;
        };
        dot(points.front());
        for (usize i = 1; i < points.size(); ++i)
        {
            const glm::vec3 a = points[i - 1], b = points[i];
            const f32 pixels = std::hypot(b.x - a.x, b.z - a.z) * scale;
            const auto steps = static_cast<usize>(std::ceil(pixels * kStepsPerPixel)) + 1;
            for (usize s = 1; s <= steps; ++s)
                dot(a + (b - a) * (static_cast<f32>(s) / static_cast<f32>(steps)));
        }
        return rgba;
    }

} // namespace rct3::park::track
