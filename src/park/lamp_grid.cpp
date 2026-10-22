// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/lamp_grid.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    LampTexels lamp_texels(std::span<const Lamp> lamps, float cell)
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
        const auto cells_x = std::clamp(static_cast<std::uint32_t>(std::ceil(span.x / cell)), 1u, kLampCellsAcross);
        const auto cells_z = std::clamp(static_cast<std::uint32_t>(std::ceil(span.y / cell)), 1u, kLampCellsAcross);

        constexpr std::uint32_t kCellTexels = kLampsPerCell / 4;
        const auto grid_start = static_cast<std::uint32_t>(2 + 2 * lamps.size());
        const std::uint32_t texels = grid_start + cells_x * cells_z * kCellTexels;
        LampTexels out { .rows = (texels + kLampTexelsAcross - 1) / kLampTexelsAcross };
        out.values.assign(std::size_t { out.rows } * kLampTexelsAcross * 4, -1.0f);
        const auto put = [&](std::uint32_t texel, glm::vec4 v) { std::ranges::copy(std::array { v.x, v.y, v.z, v.w }, out.values.begin() + std::ptrdiff_t { texel } * 4); };
        put(0, { low.x, low.y, cell, static_cast<float>(cells_x) });
        put(1, { static_cast<float>(cells_z), static_cast<float>(lamps.size()), static_cast<float>(grid_start), 0 });

        std::vector<std::uint32_t> used(std::size_t { cells_x } * cells_z, 0);
        for (std::size_t i = 0; i < lamps.size(); ++i)
        {
            const Lamp& l = lamps[i];
            put(static_cast<std::uint32_t>(2 + 2 * i), { l.position, l.radius });
            put(static_cast<std::uint32_t>(3 + 2 * i), { l.colour, 0 });
            const glm::vec2 at { l.position.x, l.position.z };
            const glm::ivec2 from = glm::clamp(glm::ivec2(glm::floor((at - l.radius - low) / cell)), glm::ivec2(0), glm::ivec2(cells_x - 1, cells_z - 1));
            const glm::ivec2 to = glm::clamp(glm::ivec2(glm::floor((at + l.radius - low) / cell)), glm::ivec2(0), glm::ivec2(cells_x - 1, cells_z - 1));
            for (int z = from.y; z <= to.y; ++z)
                for (int x = from.x; x <= to.x; ++x)
                {
                    // shortcut: a full cell takes no more lamps, so the later of crowded lamps go dark there;
                    // keep the brightest instead if crowded stations show it.
                    const std::size_t c = static_cast<std::size_t>(z) * cells_x + static_cast<std::size_t>(x);
                    if (used[c] == kLampsPerCell)
                        continue;
                    out.values[(std::size_t { grid_start } + c * kCellTexels) * 4 + used[c]++] = static_cast<float>(i);
                }
        }
        return out;
    }

} // namespace rct3::park
