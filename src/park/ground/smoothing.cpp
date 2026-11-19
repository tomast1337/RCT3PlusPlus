// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/smoothing.hpp"

#include "core/types.hpp"
#include "random/generator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/geometric.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace rct3::park::ground
{

    namespace
    {
        bool within(glm::vec2 point, glm::vec2 middle, f32 reach)
        {
            return glm::length(point - middle) <= reach;
        }

        /// How strongly a brush works at `point`: all at its middle, falling off with the square of the distance
        /// to nothing at its edge.
        f32 weight_at(glm::vec2 point, glm::vec2 middle, f32 reach)
        {
            const glm::vec2 off = (point - middle) / reach;
            return std::max(1 - glm::dot(off, off), 0.0f);
        }

        constexpr f32 kSame = 0.001f; ///< Heights closer than this are one

        /// The tile's corners, each as the point it stands at.
        constexpr glm::vec2 kCornerAt[4] { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    } // namespace

    f32 whole_level(f32 height)
    {
        return std::floor(height + 0.5f);
    }

    f32 mean_height(const save::model::TerrainGrid& grid, glm::vec2 middle, f32 reach)
    {
        f32 sum = 0;
        i32 count = 0;
        for (i32 r = 0; r < grid.height; ++r)
            for (i32 c = 0; c < grid.width; ++c)
                for (usize k = 0; k < 4; ++k)
                    if (within(glm::vec2 { c, r } + kCornerAt[k], middle, reach))
                    {
                        sum += grid.at(c, r).corners[k];
                        ++count;
                    }
        return count > 0 ? sum / static_cast<f32>(count) : 0;
    }

    f32 smooth_ground(save::model::TerrainGrid& grid, GroundSmoothing tool, glm::vec2 middle, f32 reach, f32 share, f32 most, f32 target, u64 seed, const std::function<bool(save::model::Tile)>& free)
    {
        if (reach <= 0)
            return 0;
        // Where a height goes on its way to `to`, at a point the brush works on with `weight`: never past it, and
        // all of the way once within kSettled, so the ground comes to rest.
        const auto ease = [&](f32 from, f32 to, f32 weight) {
            constexpr f32 kSettled = 0.01f; ///< In height units
            const f32 by = std::clamp((to - from) * std::min(weight * std::max(share, 0.0f), 1.0f), -weight * most, weight * most);
            return weight > 0 && std::abs(to - from - by) < kSettled ? to : from + by;
        };
        const i32 c0 = std::max(static_cast<i32>(std::floor(middle.x - reach)), 0);
        const i32 c1 = std::min(static_cast<i32>(std::ceil(middle.x + reach)), grid.width);
        const i32 r0 = std::max(static_cast<i32>(std::floor(middle.y - reach)), 0);
        const i32 r1 = std::min(static_cast<i32>(std::ceil(middle.y + reach)), grid.height);
        f32 moved = 0;

        if (tool == GroundSmoothing::TileEdges)
        {
            for (i32 r = r0; r < r1; ++r)
                for (i32 c = c0; c < c1; ++c)
                {
                    const f32 weight = weight_at(glm::vec2 { c + 0.5f, r + 0.5f }, middle, reach);
                    if (!grid.contains(c, r) || !free({ c, r }) || weight <= 0)
                        continue;
                    auto& corners = grid.at(c, r).corners;
                    const f32 level = std::round((corners[0] + corners[1] + corners[2] + corners[3]) / 4);
                    for (f32& h : corners)
                    {
                        const f32 to = ease(h, level, weight);
                        moved += std::abs(to - h);
                        h = to;
                    }
                }
            return moved;
        }

        const auto corner = [&](i32 c, i32 r, i32 dc, i32 dr) -> std::optional<f32&> {
            // The tile whose corner (1 - dc, 1 - dr) is point (c, r).
            if (!grid.contains(c - 1 + dc, r - 1 + dr))
                return std::nullopt;
            return grid.at(c - 1 + dc, r - 1 + dr).corners[static_cast<usize>((1 - dc) + 2 * (1 - dr))];
        };
        // The tiles round the points worked on as they are, for averaging: a pass reads none of its own changes.
        const i32 t0 = c0 - 2;
        const i32 across = c1 - c0 + 4;
        std::vector<std::array<f32, 4>> was;
        if (tool == GroundSmoothing::Average)
        {
            was.reserve(static_cast<usize>(across) * static_cast<usize>(r1 - r0 + 4));
            for (i32 r = r0 - 2; r <= r1 + 1; ++r)
                for (i32 c = t0; c <= c1 + 1; ++c)
                    was.push_back(grid.contains(c, r) ? grid.at(c, r).corners : std::array<f32, 4> { });
        }
        // Tile (tc, tr)'s height at point (pc, pr), one of its corners.
        const auto old = [&](i32 tc, i32 tr, i32 pc, i32 pr) { return was[static_cast<usize>(tr - r0 + 2) * static_cast<usize>(across) + static_cast<usize>(tc - t0)][static_cast<usize>((pc - tc) + 2 * (pr - tr))]; };
        // The mean of the four points next to (pc, pr) as tile (tc, tr)'s ground has them: each from the tiles
        // round the point that agree with this one there and reach that way. Where none does, past a cliff, the
        // tile's own height stands in, so each side of a cliff is averaged by itself and stays in one piece.
        const auto averaged = [&](i32 tc, i32 tr, i32 pc, i32 pr) {
            const f32 here = old(tc, tr, pc, pr);
            f32 sum = 0;
            for (const auto& [dc, dr] : { std::pair { -1, 0 }, std::pair { 1, 0 }, std::pair { 0, -1 }, std::pair { 0, 1 } })
            {
                const i32 qc = pc + dc;
                const i32 qr = pr + dr;
                f32 side = 0;
                i32 count = 0;
                for (i32 nr = pr - 1; nr <= pr; ++nr)
                    for (i32 nc = pc - 1; nc <= pc; ++nc)
                        if (grid.contains(nc, nr) && qc >= nc && qc <= nc + 1 && qr >= nr && qr <= nr + 1 && std::abs(old(nc, nr, pc, pr) - here) < kSame)
                        {
                            side += old(nc, nr, qc, qr);
                            ++count;
                        }
                sum += count > 0 ? side / static_cast<f32>(count) : here;
            }
            return sum / 4;
        };
        for (i32 r = r0; r <= r1; ++r)
            for (i32 c = c0; c <= c1; ++c)
            {
                const f32 weight = weight_at(glm::vec2 { c, r }, middle, reach);
                if (weight <= 0)
                    continue;
                bool all_free = true;
                f32 low = 1e9f;
                f32 high = -1e9f;
                f32 sum = 0;
                i32 count = 0;
                for (i32 dr = 0; dr < 2; ++dr)
                    for (i32 dc = 0; dc < 2; ++dc)
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
                u64 mix = seed * (grid.cells.size() + 1) + static_cast<usize>(r) * static_cast<usize>(grid.width + 1) + static_cast<usize>(c); // One draw per point and pass
                const f32 roll = static_cast<f32>(random::split_mix(mix) >> 40) / static_cast<f32>(1u << 24);
                for (i32 dr = 0; dr < 2; ++dr)
                    for (i32 dc = 0; dc < 2; ++dc)
                    {
                        const auto h = corner(c, r, dc, dr);
                        if (!h)
                            continue;
                        f32 to = *h;
                        switch (tool)
                        {
                            case GroundSmoothing::JoinEdges:
                                to = ease(*h, sum / static_cast<f32>(count), weight); // The one tool that closes a cliff
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
