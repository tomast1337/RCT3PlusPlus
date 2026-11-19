// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/lamp_grid.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rct3::park
{

    LampTexels lamp_texels(std::span<const Lamp> lamps, f32 cell)
    {
        if (lamps.empty())
            return { };
        glm::vec2 low { lamps[0].position.x, lamps[0].position.z };
        glm::vec2 high = low;
        for (const Lamp& l : lamps)
        {
            low = glm::min(low, glm::vec2(l.position.x, l.position.z) - l.radius);
            high = glm::max(high, glm::vec2(l.position.x, l.position.z) + l.radius);
        }
        const glm::vec2 span = high - low;
        cell = std::max({ cell, span.x / kLampCellsAcross, span.y / kLampCellsAcross, 1e-3f });
        const auto cells_x = std::clamp(static_cast<u32>(std::ceil(span.x / cell)), 1u, kLampCellsAcross);
        const auto cells_z = std::clamp(static_cast<u32>(std::ceil(span.y / cell)), 1u, kLampCellsAcross);

        constexpr u32 kCellTexels = kLampsPerCell / 4;
        const auto grid_start = static_cast<u32>(2 + 2 * lamps.size());
        const u32 texels = grid_start + cells_x * cells_z * kCellTexels;
        LampTexels out { .rows = (texels + kLampTexelsAcross - 1) / kLampTexelsAcross };
        out.values.assign(usize { out.rows } * kLampTexelsAcross * 4, -1.0f);
        const auto put = [&](u32 texel, glm::vec4 v) { std::ranges::copy(std::array { v.x, v.y, v.z, v.w }, out.values.begin() + std::ptrdiff_t { texel } * 4); };
        put(0, { low.x, low.y, cell, static_cast<f32>(cells_x) });
        put(1, { static_cast<f32>(cells_z), static_cast<f32>(lamps.size()), static_cast<f32>(grid_start), 0 });

        std::vector<u32> used(usize { cells_x } * cells_z, 0);
        for (usize i = 0; i < lamps.size(); ++i)
        {
            const Lamp& l = lamps[i];
            put(static_cast<u32>(2 + 2 * i), { l.position, l.radius });
            put(static_cast<u32>(3 + 2 * i), { l.colour, 0 });
            const glm::vec2 at { l.position.x, l.position.z };
            const glm::ivec2 from = glm::clamp(glm::ivec2(glm::floor((at - l.radius - low) / cell)), glm::ivec2(0), glm::ivec2(cells_x - 1, cells_z - 1));
            const glm::ivec2 to = glm::clamp(glm::ivec2(glm::floor((at + l.radius - low) / cell)), glm::ivec2(0), glm::ivec2(cells_x - 1, cells_z - 1));
            for (i32 z = from.y; z <= to.y; ++z)
                for (i32 x = from.x; x <= to.x; ++x)
                {
                    // shortcut: a full cell takes no more lamps, so the later of crowded lamps go dark there;
                    // keep the brightest instead if crowded stations show it.
                    const usize c = static_cast<usize>(z) * cells_x + static_cast<usize>(x);
                    if (used[c] == kLampsPerCell)
                        continue;
                    out.values[(usize { grid_start } + c * kCellTexels) * 4 + used[c]++] = static_cast<f32>(i);
                }
        }
        return out;
    }

} // namespace rct3::park
