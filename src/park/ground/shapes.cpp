// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/shapes.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <numbers>

namespace rct3::park::ground
{

    namespace
    {
        constexpr float kMesaTop = 0.6f;     ///< Of the reach, flat on top
        constexpr float kCanyonFloor = 0.6f; ///< Of the reach, flat at the bottom
        constexpr float kRidgeWidth = 0.35f; ///< Of the reach, across the ridge
        constexpr float kCraterBowl = 0.75f; ///< Of the reach, the bowl inside the rim
        constexpr float kCraterRim = 0.25f;  ///< Of the amount, how high the rim stands

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

    float drag_amount(float pixels)
    {
        return std::copysign(std::max(std::abs(pixels) - kDragDeadZone, 0.0f) * kDragRate, pixels);
    }

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

    GroundBefore ground_before(const save::model::TerrainGrid& grid, const std::function<bool(save::model::Tile)>& free)
    {
        GroundBefore out { .width = grid.width };
        const auto across = static_cast<std::size_t>(grid.width + 1);
        out.corners.reserve(grid.cells.size());
        out.held.assign(across * static_cast<std::size_t>(grid.height + 1), false);
        for (int r = 0; r < grid.height; ++r)
            for (int c = 0; c < grid.width; ++c)
            {
                out.corners.push_back(grid.at(c, r).corners);
                if (free({ c, r }))
                    continue;
                for (std::size_t k = 0; k < 4; ++k)
                    out.held[(static_cast<std::size_t>(r) + k / 2) * across + static_cast<std::size_t>(c) + k % 2] = true;
            }
        return out;
    }

    float shape_ground(save::model::TerrainGrid& grid, const GroundBefore& before, GroundShape shape, glm::vec2 middle, float reach, glm::vec2 along, float amount)
    {
        if (reach <= 0 || before.width != grid.width || before.corners.size() != grid.cells.size())
            return 0;
        const glm::vec2 line = glm::length(along) > 0 ? glm::normalize(along) : glm::vec2 { 1, 0 };
        const glm::vec2 side { -line.y, line.x };
        const auto across = static_cast<std::size_t>(grid.width + 1);
        float moved = 0;
        // The tiles with a corner point inside the reach.
        const int c0 = std::max(static_cast<int>(std::floor(middle.x - reach)) - 1, 0);
        const int c1 = std::min(static_cast<int>(std::ceil(middle.x + reach)), grid.width - 1);
        const int r0 = std::max(static_cast<int>(std::floor(middle.y - reach)) - 1, 0);
        const int r1 = std::min(static_cast<int>(std::ceil(middle.y + reach)), grid.height - 1);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
            {
                const auto& was = before.corners[static_cast<std::size_t>(r) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(c)];
                auto& now = grid.at(c, r).corners;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    const int pc = c + static_cast<int>(k % 2);
                    const int pr = r + static_cast<int>(k / 2);
                    if (before.held[static_cast<std::size_t>(pr) * across + static_cast<std::size_t>(pc)])
                        continue;
                    const glm::vec2 offset = glm::vec2 { pc, pr } - middle;
                    const float share = shape == GroundShape::Ridge ? shape_share(shape, glm::dot(offset, side) / reach, glm::dot(offset, line) / reach) : shape_share(shape, glm::length(offset) / reach);
                    now[k] = was[k] + share * amount;
                    // Measured from the stroke's start, so the owner pays the difference between two calls.
                    moved += std::abs(share * amount);
                }
            }
        return moved;
    }

} // namespace rct3::park::ground
