// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/smoothing.hpp"

#include "random/generator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace rct3::park::ground
{

    namespace
    {
        bool within(glm::vec2 point, glm::vec2 middle, float reach)
        {
            return glm::length(point - middle) <= reach;
        }

        /// How strongly a brush works at `point`: all at its middle, falling off with the square of the distance
        /// to nothing at its edge.
        float weight_at(glm::vec2 point, glm::vec2 middle, float reach)
        {
            const glm::vec2 off = (point - middle) / reach;
            return std::max(1 - glm::dot(off, off), 0.0f);
        }

        constexpr float kSame = 0.001f; ///< Heights closer than this are one

        /// The tile's corners, each as the point it stands at.
        constexpr glm::vec2 kCornerAt[4] { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    } // namespace

    float whole_level(float height)
    {
        return std::floor(height + 0.5f);
    }

    float mean_height(const save::model::TerrainGrid& grid, glm::vec2 middle, float reach)
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

    float smooth_ground(save::model::TerrainGrid& grid, GroundSmoothing tool, glm::vec2 middle, float reach, float share, float most, float target, std::uint64_t seed, const std::function<bool(save::model::Tile)>& free)
    {
        if (reach <= 0)
            return 0;
        // Where a height goes on its way to `to`, at a point the brush works on with `weight`: never past it, and
        // all of the way once within kSettled, so the ground comes to rest.
        const auto ease = [&](float from, float to, float weight) {
            constexpr float kSettled = 0.01f; ///< In height units
            const float by = std::clamp((to - from) * std::min(weight * std::max(share, 0.0f), 1.0f), -weight * most, weight * most);
            return weight > 0 && std::abs(to - from - by) < kSettled ? to : from + by;
        };
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
                    const float weight = weight_at(glm::vec2 { c + 0.5f, r + 0.5f }, middle, reach);
                    if (!grid.contains(c, r) || !free({ c, r }) || weight <= 0)
                        continue;
                    auto& corners = grid.at(c, r).corners;
                    const float level = std::round((corners[0] + corners[1] + corners[2] + corners[3]) / 4);
                    for (float& h : corners)
                    {
                        const float to = ease(h, level, weight);
                        moved += std::abs(to - h);
                        h = to;
                    }
                }
            return moved;
        }

        const auto corner = [&](int c, int r, int dc, int dr) -> std::optional<float&> {
            // The tile whose corner (1 - dc, 1 - dr) is point (c, r).
            if (!grid.contains(c - 1 + dc, r - 1 + dr))
                return std::nullopt;
            return grid.at(c - 1 + dc, r - 1 + dr).corners[static_cast<std::size_t>((1 - dc) + 2 * (1 - dr))];
        };
        // The tiles round the points worked on as they are, for averaging: a pass reads none of its own changes.
        const int t0 = c0 - 2;
        const int across = c1 - c0 + 4;
        std::vector<std::array<float, 4>> was;
        if (tool == GroundSmoothing::Average)
        {
            was.reserve(static_cast<std::size_t>(across) * static_cast<std::size_t>(r1 - r0 + 4));
            for (int r = r0 - 2; r <= r1 + 1; ++r)
                for (int c = t0; c <= c1 + 1; ++c)
                    was.push_back(grid.contains(c, r) ? grid.at(c, r).corners : std::array<float, 4> { });
        }
        // Tile (tc, tr)'s height at point (pc, pr), one of its corners.
        const auto old = [&](int tc, int tr, int pc, int pr) { return was[static_cast<std::size_t>(tr - r0 + 2) * static_cast<std::size_t>(across) + static_cast<std::size_t>(tc - t0)][static_cast<std::size_t>((pc - tc) + 2 * (pr - tr))]; };
        // The mean of the four points next to (pc, pr) as tile (tc, tr)'s ground has them: each from the tiles
        // round the point that agree with this one there and reach that way. Where none does, past a cliff, the
        // tile's own height stands in, so each side of a cliff is averaged by itself and stays in one piece.
        const auto averaged = [&](int tc, int tr, int pc, int pr) {
            const float here = old(tc, tr, pc, pr);
            float sum = 0;
            for (const auto& [dc, dr] : { std::pair { -1, 0 }, std::pair { 1, 0 }, std::pair { 0, -1 }, std::pair { 0, 1 } })
            {
                const int qc = pc + dc;
                const int qr = pr + dr;
                float side = 0;
                int count = 0;
                for (int nr = pr - 1; nr <= pr; ++nr)
                    for (int nc = pc - 1; nc <= pc; ++nc)
                        if (grid.contains(nc, nr) && qc >= nc && qc <= nc + 1 && qr >= nr && qr <= nr + 1 && std::abs(old(nc, nr, pc, pr) - here) < kSame)
                        {
                            side += old(nc, nr, qc, qr);
                            ++count;
                        }
                sum += count > 0 ? side / static_cast<float>(count) : here;
            }
            return sum / 4;
        };
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
            {
                const float weight = weight_at(glm::vec2 { c, r }, middle, reach);
                if (weight <= 0)
                    continue;
                bool all_free = true;
                float low = 1e9f;
                float high = -1e9f;
                float sum = 0;
                int count = 0;
                for (int dr = 0; dr < 2; ++dr)
                    for (int dc = 0; dc < 2; ++dc)
                        if (const auto h = corner(c, r, dc, dr))
                        {
                            all_free = all_free && free({ c - 1 + dc, r - 1 + dr });
                            low = std::min(low, *h);
                            high = std::max(high, *h);
                            sum += *h;
                            ++count;
                        }
                if (!all_free || count == 0 || (tool == GroundSmoothing::JoinEdges && high - low <= 0))
                    continue;
                // Roughening moves the tiles' corners here by one amount, a cliff between them keeping its height.
                std::uint64_t mix = seed * (grid.cells.size() + 1) + static_cast<std::size_t>(r) * static_cast<std::size_t>(grid.width + 1) + static_cast<std::size_t>(c); // One draw per point and pass
                const float roll = static_cast<float>(random::split_mix(mix) >> 40) / static_cast<float>(1u << 24);
                for (int dr = 0; dr < 2; ++dr)
                    for (int dc = 0; dc < 2; ++dc)
                    {
                        const auto h = corner(c, r, dc, dr);
                        if (!h)
                            continue;
                        float to = *h;
                        switch (tool)
                        {
                            case GroundSmoothing::JoinEdges:
                                to = ease(*h, sum / static_cast<float>(count), weight); // The one tool that closes a cliff
                                break;
                            case GroundSmoothing::Average:
                                to = ease(*h, averaged(c - 1 + dc, r - 1 + dr, c, r), weight);
                                break;
                            case GroundSmoothing::Random:
                                to = ease(*h, *h + roll - 0.5f, weight);
                                break;
                            case GroundSmoothing::Level:
                                to = ease(*h, target, weight);
                                break;
                            default:
                                break;
                        }
                        moved += std::abs(to - *h);
                        *h = to;
                    }
            }
        return moved;
    }

} // namespace rct3::park::ground
