// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_shapes.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <numbers>

namespace rct3::park
{

    namespace
    {
        // Our own values.
        constexpr float kMesaTop = 0.6f;     ///< Of the reach, flat on top
        constexpr float kCanyonFloor = 0.6f; ///< Of the reach, flat at the bottom
        constexpr float kRidgeWidth = 0.35f; ///< Of the reach, across the ridge
        constexpr float kCraterBowl = 0.75f; ///< Of the reach, the bowl inside the rim
        constexpr float kCraterRim = 0.25f;  ///< Of the amount, how high the rim stands
        constexpr float kSame = 0.001f;      ///< Heights closer than this are one

        /// 1 at 0, easing down to 0 at 1.
        float bump(float d)
        {
            return d >= 1 ? 0 : 0.5f * (1 + std::cos(std::numbers::pi_v<float> * std::max(d, 0.0f)));
        }

        /// 1 up to `top`, easing down to 0 at 1.
        float plateau(float d, float top)
        {
            return d <= top ? 1 : bump((d - top) / (1 - top));
        }
    } // namespace

    float shape_share(GroundShape shape, float across, float along)
    {
        across = std::abs(across);
        switch (shape)
        {
            case GroundShape::Hill:
                return bump(across);
            case GroundShape::Mountain:
            {
                const float d = std::clamp(1 - across, 0.0f, 1.0f);
                return d * d * d;
            }
            case GroundShape::Mesa:
                return plateau(across, kMesaTop);
            case GroundShape::Ridge:
                return bump(across / kRidgeWidth) * bump(std::abs(along));
            case GroundShape::Canyon:
                return -plateau(across, kCanyonFloor);
            case GroundShape::Trough:
            {
                const float d = bump(across);
                return -std::sqrt(d) * 0.5f - d * 0.5f; // Wide and shallow-sided
            }
            case GroundShape::Crater:
            {
                if (across <= kCraterBowl)
                    return -bump(across / kCraterBowl) + kCraterRim * (across / kCraterBowl);
                return kCraterRim * bump((across - kCraterBowl) / (1 - kCraterBowl));
            }
        }
        return 0;
    }

    CornerHeights corner_heights(const save::TerrainGrid& grid, const std::function<bool(int col, int row)>& free)
    {
        CornerHeights out;
        out.columns = grid.width + 1;
        const auto points = static_cast<std::size_t>(out.columns) * static_cast<std::size_t>(grid.height + 1);
        out.heights.assign(points, 0);
        out.held.assign(points, false);
        for (int r = 0; r <= grid.height; ++r)
            for (int c = 0; c <= grid.width; ++c)
            {
                const auto at = static_cast<std::size_t>(r) * static_cast<std::size_t>(out.columns) + static_cast<std::size_t>(c);
                bool first = true;
                for (int dr = 0; dr < 2; ++dr)
                    for (int dc = 0; dc < 2; ++dc)
                    {
                        // The tile whose corner (1 - dc, 1 - dr) is this point.
                        const int tc = c - 1 + dc;
                        const int tr = r - 1 + dr;
                        if (!grid.contains(tc, tr))
                            continue;
                        const float h = grid.at(tc, tr).corners[static_cast<std::size_t>((1 - dc) + 2 * (1 - dr))];
                        if (first)
                            out.heights[at] = h;
                        else if (std::abs(h - out.heights[at]) > kSame)
                            out.held[at] = true;
                        first = false;
                        if (!free(tc, tr))
                            out.held[at] = true;
                    }
            }
        return out;
    }

    float shape_ground(save::TerrainGrid& grid, const CornerHeights& before, GroundShape shape, glm::vec2 middle, float reach, glm::vec2 along, float amount)
    {
        if (reach <= 0 || before.columns != grid.width + 1)
            return 0;
        const glm::vec2 line = glm::length(along) > 0 ? glm::normalize(along) : glm::vec2 { 1, 0 };
        const glm::vec2 side { -line.y, line.x };
        float moved = 0;
        const int c0 = std::max(static_cast<int>(std::floor(middle.x - reach)), 0);
        const int c1 = std::min(static_cast<int>(std::ceil(middle.x + reach)), grid.width);
        const int r0 = std::max(static_cast<int>(std::floor(middle.y - reach)), 0);
        const int r1 = std::min(static_cast<int>(std::ceil(middle.y + reach)), grid.height);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
            {
                const auto at = static_cast<std::size_t>(r) * static_cast<std::size_t>(before.columns) + static_cast<std::size_t>(c);
                if (before.held[at])
                    continue;
                const glm::vec2 offset = glm::vec2 { c, r } - middle;
                const float share = shape == GroundShape::Ridge ? shape_share(shape, glm::dot(offset, side) / reach, glm::dot(offset, line) / reach) : shape_share(shape, glm::length(offset) / reach);
                const float height = before.heights[at] + share * amount;
                for (int dr = 0; dr < 2; ++dr)
                    for (int dc = 0; dc < 2; ++dc)
                    {
                        const int tc = c - 1 + dc;
                        const int tr = r - 1 + dr;
                        if (!grid.contains(tc, tr))
                            continue;
                        grid.at(tc, tr).corners[static_cast<std::size_t>((1 - dc) + 2 * (1 - dr))] = height;
                    }
                // Measured from the stroke's start, so the owner pays the difference between two calls.
                moved += std::abs(height - before.heights[at]);
            }
        return moved;
    }

} // namespace rct3::park
