// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track/top_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/common.hpp>

namespace rct3::park::track
{

    namespace
    {
        constexpr float kMargin = 8;         ///< Chosen by eye: pixels kept clear round the line
        constexpr std::uint8_t kLowest = 96; ///< Chosen by eye: the level of the lowest track, still plain on black
        constexpr float kStepsPerPixel = 2;  ///< Dots along each stretch, so the line has no gaps
    } // namespace

    std::vector<std::uint8_t> top_view(std::span<const glm::vec3> points, std::uint32_t size)
    {
        if (points.empty() || size == 0)
            return { };
        glm::vec3 low = points.front(), high = points.front();
        for (const glm::vec3& p : points)
        {
            low = glm::min(low, p);
            high = glm::max(high, p);
        }
        const float room = std::max(static_cast<float>(size) - 2 * kMargin, 1.0f);
        const float scale = room / std::max({ high.x - low.x, high.z - low.z, 1e-3f });
        // Centred both ways.
        const float left = kMargin + (room - (high.x - low.x) * scale) / 2;
        const float top = kMargin + (room - (high.z - low.z) * scale) / 2;
        const float rise = high.y - low.y;

        std::vector<std::uint8_t> rgba(std::size_t { size } * size * 4, 0);
        const auto dot = [&](glm::vec3 p) {
            const auto x = static_cast<std::int64_t>(std::floor(left + (p.x - low.x) * scale));
            const auto y = static_cast<std::int64_t>(std::floor(top + (p.z - low.z) * scale));
            if (x < 0 || y < 0 || x >= size || y >= size)
                return;
            const auto level = static_cast<std::uint8_t>(kLowest + std::lround((255 - kLowest) * (rise > 0 ? (p.y - low.y) / rise : 1.0f)));
            const std::size_t at = (static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 4;
            // Where the track passes over itself, the higher stretch shows.
            if (rgba[at + 3] != 0 && rgba[at] >= level)
                return;
            rgba[at] = rgba[at + 1] = rgba[at + 2] = level;
            rgba[at + 3] = 255;
        };
        dot(points.front());
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const glm::vec3 a = points[i - 1], b = points[i];
            const float pixels = std::hypot(b.x - a.x, b.z - a.z) * scale;
            const auto steps = static_cast<std::size_t>(std::ceil(pixels * kStepsPerPixel)) + 1;
            for (std::size_t s = 1; s <= steps; ++s)
                dot(a + (b - a) * (static_cast<float>(s) / static_cast<float>(steps)));
        }
        return rgba;
    }

} // namespace rct3::park::track
