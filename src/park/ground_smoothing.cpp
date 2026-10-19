// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_smoothing.hpp"

#include "park/ground_shapes.hpp"
#include "random/generator.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <vector>

namespace rct3::park
{

    namespace
    {
        bool within(glm::vec2 point, glm::vec2 middle, float reach)
        {
            return glm::length(point - middle) <= reach;
        }

        /// Part of the way from `from` to `to`, all of it once within kSettled, so the ground comes to rest.
        float ease(float from, float to, float strength)
        {
            constexpr float kSettled = 0.01f; ///< In height units. Our own value.
            const float next = from + (to - from) * strength;
            return std::abs(to - next) < kSettled ? to : next;
        }

        /// The tile's corners, each as the point it stands at.
        constexpr glm::vec2 kCornerAt[4] { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    } // namespace

    float mean_height(const save::TerrainGrid& grid, glm::vec2 middle, float reach)
    {
        float sum = 0;
        int count = 0;
        for (int r = 0; r < grid.height; ++r)
            for (int c = 0; c < grid.width; ++c)
                for (std::size_t k = 0; k < 4; ++k)
                    if (within(glm::vec2 { c, r } + kCornerAt[k], middle, reach))
                    {
                        sum += grid.at(c, r).corners[k];
                        ++count;
                    }
        return count > 0 ? sum / static_cast<float>(count) : 0;
    }

    float smooth_ground(save::TerrainGrid& grid, GroundSmoothing tool, glm::vec2 middle, float reach, float strength, float target, std::uint64_t seed, const std::function<bool(int col, int row)>& free)
    {
        strength = std::clamp(strength, 0.0f, 1.0f);
        const int c0 = std::max(static_cast<int>(std::floor(middle.x - reach)), 0);
        const int c1 = std::min(static_cast<int>(std::ceil(middle.x + reach)), grid.width);
        const int r0 = std::max(static_cast<int>(std::floor(middle.y - reach)), 0);
        const int r1 = std::min(static_cast<int>(std::ceil(middle.y + reach)), grid.height);
        float moved = 0;

        if (tool == GroundSmoothing::TileEdges)
        {
            for (int r = r0; r < r1; ++r)
                for (int c = c0; c < c1; ++c)
                {
                    if (!grid.contains(c, r) || !free(c, r) || !within(glm::vec2 { c + 0.5f, r + 0.5f }, middle, reach))
                        continue;
                    auto& corners = grid.at(c, r).corners;
                    const float level = std::round((corners[0] + corners[1] + corners[2] + corners[3]) / 4);
                    for (float& h : corners)
                    {
                        const float to = ease(h, level, strength);
                        moved += std::abs(to - h);
                        h = to;
                    }
                }
            return moved;
        }

        // The points as they are, the tiles round each settling on one height or holding it.
        const CornerHeights before = corner_heights(grid, free);
        const auto index = [&](int c, int r) { return static_cast<std::size_t>(r) * static_cast<std::size_t>(before.columns) + static_cast<std::size_t>(c); };
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
            {
                if (!within(glm::vec2 { c, r }, middle, reach))
                    continue;
                const std::size_t at = index(c, r);
                if (tool == GroundSmoothing::JoinEdges)
                {
                    // Held for a cliff is what this tool undoes; held for a tile it may not change is not.
                    bool all_free = true;
                    float sum = 0;
                    float low = 1e9f;
                    float high = -1e9f;
                    int count = 0;
                    for (int dr = 0; dr < 2; ++dr)
                        for (int dc = 0; dc < 2; ++dc)
                        {
                            const int tc = c - 1 + dc;
                            const int tr = r - 1 + dr;
                            if (!grid.contains(tc, tr))
                                continue;
                            all_free = all_free && free(tc, tr);
                            const float h = grid.at(tc, tr).corners[static_cast<std::size_t>((1 - dc) + 2 * (1 - dr))];
                            sum += h;
                            low = std::min(low, h);
                            high = std::max(high, h);
                            ++count;
                        }
                    if (!all_free || count == 0 || high - low <= 0)
                        continue;
                    const float mean = sum / static_cast<float>(count);
                    for (int dr = 0; dr < 2; ++dr)
                        for (int dc = 0; dc < 2; ++dc)
                        {
                            if (!grid.contains(c - 1 + dc, r - 1 + dr))
                                continue;
                            float& h = grid.at(c - 1 + dc, r - 1 + dr).corners[static_cast<std::size_t>((1 - dc) + 2 * (1 - dr))];
                            const float to = ease(h, mean, strength);
                            moved += std::abs(to - h);
                            h = to;
                        }
                    continue;
                }
                if (before.held[at])
                    continue;
                const float h = before.heights[at];
                float goal = h;
                switch (tool)
                {
                    case GroundSmoothing::Average:
                    {
                        float sum = 0;
                        int count = 0;
                        for (const auto& [dc, dr] : { std::pair { -1, 0 }, std::pair { 1, 0 }, std::pair { 0, -1 }, std::pair { 0, 1 } })
                            if (c + dc >= 0 && r + dr >= 0 && c + dc < before.columns && r + dr <= grid.height)
                            {
                                sum += before.heights[index(c + dc, r + dr)];
                                ++count;
                            }
                        goal = count > 0 ? sum / static_cast<float>(count) : h;
                        break;
                    }
                    case GroundSmoothing::Random:
                    {
                        std::uint64_t mix = seed * (before.heights.size() + 1) + at; // One draw per point and pass
                        const float roll = static_cast<float>(random::split_mix(mix) >> 40) / static_cast<float>(1u << 24);
                        goal = h + roll - 0.5f;
                        break;
                    }
                    case GroundSmoothing::Level:
                        goal = target;
                        break;
                    case GroundSmoothing::WholeUnits:
                        goal = std::round(h);
                        break;
                    default:
                        break;
                }
                const float height = h + (goal - h) * strength;
                moved += std::abs(height - h);
                set_corner_point(grid, c, r, height);
            }
        return moved;
    }

} // namespace rct3::park
