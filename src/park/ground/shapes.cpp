// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/shapes.hpp"

#include "core/types.hpp"
#include "math/portable.hpp"
#include "random/generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/geometric.hpp>
#include <numbers>

namespace rct3::park::ground
{

    namespace
    {
        constexpr f32 kMesaTop = 0.6f;     ///< Of the reach, flat on top
        constexpr f32 kCanyonFloor = 0.6f; ///< Of the reach, flat at the bottom
        constexpr f32 kRidgeWidth = 0.35f; ///< Of the reach, across the ridge
        constexpr f32 kCanyonWidth = 0.5f; ///< Of the reach, across the canyon
        constexpr f32 kCraterBowl = 0.75f; ///< Of the reach, the bowl inside the rim
        constexpr f32 kCraterRim = 0.25f;  ///< Of the amount, how high the rim stands

        /// 1 at 0, easing down to 0 at 1.
        f32 bump(f32 d)
        {
            return d >= 1 ? 0 : 0.5f * (1 + math::cos(std::numbers::pi_v<f32> * std::max(d, 0.0f)));
        }

        /// 1 up to `top`, easing down to 0 at 1.
        f32 plateau(f32 d, f32 top)
        {
            return d <= top ? 1 : bump((d - top) / (1 - top));
        }
    } // namespace

    f32 drag_amount(f32 pixels)
    {
        return std::copysign(std::max(std::abs(pixels) - kDragDeadZone.value, 0.0f) * kDragRate, pixels);
    }

    f32 shape_share(GroundShape shape, f32 across, f32 along)
    {
        across = std::abs(across);
        switch (shape)
        {
            case GroundShape::Hill:
                return bump(across);
            case GroundShape::Mountain:
            {
                const f32 d = std::clamp(1 - across, 0.0f, 1.0f);
                return d * d * d;
            }
            case GroundShape::Mesa:
                return plateau(across, kMesaTop);
            case GroundShape::Ridge:
                return bump(across / kRidgeWidth) * bump(std::abs(along));
            case GroundShape::Canyon:
                return -plateau(across / kCanyonWidth, kCanyonFloor) * bump(std::abs(along));
            case GroundShape::Trough:
            {
                const f32 d = bump(across);
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

    f32 shape_roughness(GroundShape shape)
    {
        switch (shape)
        {
            case GroundShape::Hill:
                return 0.025f;
            case GroundShape::Trough:
                return 0.05f;
            case GroundShape::Mesa:
                return 0.1f;
            case GroundShape::Mountain:
            case GroundShape::Canyon:
                return 0.2f;
            case GroundShape::Crater:
                return 0.4f;
            case GroundShape::Ridge:
                return 0.5f;
        }
        return 0;
    }

    GroundBefore ground_before(const save::model::TerrainGrid& grid, const std::function<bool(save::model::Tile)>& free)
    {
        GroundBefore out { .width = grid.width };
        const auto across = static_cast<usize>(grid.width + 1);
        out.corners.reserve(grid.cells.size());
        out.held.assign(across * static_cast<usize>(grid.height + 1), false);
        for (i32 r = 0; r < grid.height; ++r)
            for (i32 c = 0; c < grid.width; ++c)
            {
                out.corners.push_back(grid.at(c, r).corners);
                if (free({ c, r }))
                    continue;
                for (usize k = 0; k < 4; ++k)
                    out.held[(static_cast<usize>(r) + k / 2) * across + static_cast<usize>(c) + k % 2] = true;
            }
        return out;
    }

    f32 shape_ground(save::model::TerrainGrid& grid, const GroundBefore& before, GroundShape shape, glm::vec2 middle, f32 reach, glm::vec2 along, f32 amount, u64 seed)
    {
        if (reach <= 0 || before.width != grid.width || before.corners.size() != grid.cells.size())
            return 0;
        const glm::vec2 line = glm::length(along) > 0 ? glm::normalize(along) : glm::vec2 { 1, 0 };
        const glm::vec2 side { -line.y, line.x };
        const auto across = static_cast<usize>(grid.width + 1);
        f32 moved = 0;
        // The tiles with a corner point inside the reach.
        const i32 c0 = std::max(static_cast<i32>(std::floor(middle.x - reach)) - 1, 0);
        const i32 c1 = std::min(static_cast<i32>(std::ceil(middle.x + reach)), grid.width - 1);
        const i32 r0 = std::max(static_cast<i32>(std::floor(middle.y - reach)) - 1, 0);
        const i32 r1 = std::min(static_cast<i32>(std::ceil(middle.y + reach)), grid.height - 1);
        for (i32 r = r0; r <= r1; ++r)
            for (i32 c = c0; c <= c1; ++c)
            {
                const auto& was = before.corners[static_cast<usize>(r) * static_cast<usize>(grid.width) + static_cast<usize>(c)];
                auto& now = grid.at(c, r).corners;
                for (usize k = 0; k < 4; ++k)
                {
                    const i32 pc = c + static_cast<i32>(k % 2);
                    const i32 pr = r + static_cast<i32>(k / 2);
                    if (before.held[static_cast<usize>(pr) * across + static_cast<usize>(pc)])
                        continue;
                    const glm::vec2 offset = glm::vec2 { pc, pr } - middle;
                    const bool lined = shape == GroundShape::Ridge || shape == GroundShape::Canyon;
                    // Rough ground: each corner point a little off the shape, most at the middle, by a draw of its
                    // own that every tile round it shares, so the ground stays in one piece.
                    u64 mix = seed * (grid.cells.size() + 1) + static_cast<usize>(pr) * across + static_cast<usize>(pc);
                    const f32 roll = static_cast<f32>(random::split_mix(mix) >> 40) / static_cast<f32>(1u << 24);
                    const f32 rough = shape_roughness(shape) * (0.5f - roll) * std::max(1 - glm::length(offset) / reach, 0.0f);
                    const f32 share = (lined ? shape_share(shape, glm::dot(offset, side) / reach, glm::dot(offset, line) / reach) : shape_share(shape, glm::length(offset) / reach)) + rough;
                    now[k] = was[k] + share * amount;
                    // Measured from the stroke's start, so the owner pays the difference between two calls.
                    moved += std::abs(share * amount);
                }
            }
        return moved;
    }

} // namespace rct3::park::ground
