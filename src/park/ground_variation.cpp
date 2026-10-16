// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_variation.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    namespace
    {
        /// A value in [0, 1] for a lattice point of `channel`, wrapping round so the picture tiles.
        float lattice(std::uint32_t x, std::uint32_t y, std::uint32_t channel)
        {
            std::uint32_t h = (x % kVariationCells) * 0x8da6b343u ^ (y % kVariationCells) * 0xd8163841u ^ channel * 0xcb1ab31fu;
            h = (h ^ (h >> 15)) * 0x2c1b3c6du;
            h ^= h >> 12;
            return static_cast<float>(h & 0xffffu) / 65535.0f;
        }

        float smooth(float t)
        {
            return t * t * (3.0f - 2.0f * t);
        }
    } // namespace

    std::vector<std::byte> variation_texels()
    {
        constexpr float kStep = static_cast<float>(kVariationCells) / kVariationSize;
        std::vector<std::byte> texels(static_cast<std::size_t>(kVariationSize) * kVariationSize * 4);
        for (std::uint32_t y = 0; y < kVariationSize; ++y)
        {
            for (std::uint32_t x = 0; x < kVariationSize; ++x)
            {
                const float fx = (static_cast<float>(x) + 0.5f) * kStep;
                const float fy = (static_cast<float>(y) + 0.5f) * kStep;
                const auto cx = static_cast<std::uint32_t>(fx);
                const auto cy = static_cast<std::uint32_t>(fy);
                const float sx = smooth(fx - static_cast<float>(cx));
                const float sy = smooth(fy - static_cast<float>(cy));
                for (std::uint32_t c = 0; c < 4; ++c)
                {
                    const float near = std::lerp(lattice(cx, cy, c), lattice(cx + 1, cy, c), sx);
                    const float far = std::lerp(lattice(cx, cy + 1, c), lattice(cx + 1, cy + 1, c), sx);
                    const float v = std::lerp(near, far, sy);
                    texels[((static_cast<std::size_t>(y) * kVariationSize + x) * 4) + c] = static_cast<std::byte>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
                }
            }
        }
        return texels;
    }

} // namespace rct3::park
