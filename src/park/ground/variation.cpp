// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/variation.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park::ground
{

    namespace
    {
        /// A value in [0, 1] for a lattice point of `channel`, wrapping round so the picture tiles.
        f32 lattice(u32 x, u32 y, u32 channel)
        {
            u32 h = (x % kVariationCells) * 0x8da6b343u ^ (y % kVariationCells) * 0xd8163841u ^ channel * 0xcb1ab31fu;
            h = (h ^ (h >> 15)) * 0x2c1b3c6du;
            h ^= h >> 12;
            return static_cast<f32>(h & 0xffffu) / 65535.0f;
        }

        f32 smooth(f32 t)
        {
            return t * t * (3.0f - 2.0f * t);
        }
    } // namespace

    std::vector<std::byte> variation_texels()
    {
        constexpr f32 kStep = static_cast<f32>(kVariationCells) / kVariationSize;
        std::vector<std::byte> texels(static_cast<usize>(kVariationSize) * kVariationSize * 4);
        for (u32 y = 0; y < kVariationSize; ++y)
        {
            for (u32 x = 0; x < kVariationSize; ++x)
            {
                const f32 fx = (static_cast<f32>(x) + 0.5f) * kStep;
                const f32 fy = (static_cast<f32>(y) + 0.5f) * kStep;
                const auto cx = static_cast<u32>(fx);
                const auto cy = static_cast<u32>(fy);
                const f32 sx = smooth(fx - static_cast<f32>(cx));
                const f32 sy = smooth(fy - static_cast<f32>(cy));
                for (u32 c = 0; c < 4; ++c)
                {
                    const f32 near = std::lerp(lattice(cx, cy, c), lattice(cx + 1, cy, c), sx);
                    const f32 far = std::lerp(lattice(cx, cy + 1, c), lattice(cx + 1, cy + 1, c), sx);
                    const f32 v = std::lerp(near, far, sy);
                    texels[((static_cast<usize>(y) * kVariationSize + x) * 4) + c] = static_cast<std::byte>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
                }
            }
        }
        return texels;
    }

} // namespace rct3::park::ground
