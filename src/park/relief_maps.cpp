// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/relief_maps.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace rct3::park
{

    namespace
    {
        // To be tuned by eye on the game's ground, brick and rock pictures.
        constexpr i32 kFineRadius = 1;   ///< Smooths texel noise before it is read as height
        constexpr i32 kBroadRadius = 6;  ///< Shading broader than this is taken as colour, not height
        constexpr f32 kSpread = 4.0f;    ///< Fine detail this many standard deviations across fills the height's range
        constexpr f32 kSlope = 2.0f;     ///< How much a step in height leans the surface
        constexpr i32 kHollowRadius = 3; ///< A point lower than its surroundings this far round is in a hollow
        constexpr f32 kOcclusion = 0.7f; ///< How much of the sky a hollow as deep as the height's range hides
        constexpr f32 kNoDetail = 1e-4f; ///< A picture whose detail varies less than this is flat

        /// `count` values from `in`, `stride` apart, averaged over `radius` values each side, wrapping round, into
        /// `out` at the same places: a running sum, so the cost does not grow with the radius.
        void blur_line(const f32* in, f32* out, i32 count, i32 stride, i32 radius)
        {
            const auto at = [&](i32 i) { return in[static_cast<std::ptrdiff_t>((((i % count) + count) % count) * stride)]; };
            const f32 scale = 1.0f / static_cast<f32>(2 * radius + 1);
            f32 sum = 0;
            for (i32 d = -radius; d <= radius; ++d)
                sum += at(d);
            for (i32 i = 0; i < count; ++i)
            {
                out[static_cast<std::ptrdiff_t>(i * stride)] = sum * scale;
                sum += at(i + radius + 1) - at(i - radius);
            }
        }

        /// `values` (w by h) averaged over a square `radius` texels each side, wrapping round.
        std::vector<f32> box_blur(const std::vector<f32>& values, i32 w, i32 h, i32 radius)
        {
            std::vector<f32> across(values.size());
            for (i32 y = 0; y < h; ++y)
                blur_line(&values[static_cast<usize>(y * w)], &across[static_cast<usize>(y * w)], w, 1, radius);
            std::vector<f32> out(values.size());
            for (i32 x = 0; x < w; ++x)
                blur_line(&across[static_cast<usize>(x)], &out[static_cast<usize>(x)], h, w, radius);
            return out;
        }

        u8 unit_byte(f32 v)
        {
            return static_cast<u8>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        }
    } // namespace

    ovl::texture::RgbaImage relief_map(const ovl::texture::RgbaImage& picture, f32 strength)
    {
        const i32 w = static_cast<i32>(picture.width);
        const i32 h = static_cast<i32>(picture.height);
        const auto count = static_cast<usize>(w) * static_cast<usize>(h);
        ovl::texture::RgbaImage map { picture.width, picture.height, std::vector<u8>(count * 4) };
        if (count == 0 || picture.pixels.size() < count * 4)
            return map;

        std::vector<f32> light(count);
        for (usize i = 0; i < count; ++i)
        {
            const u8* p = &picture.pixels[i * 4];
            light[i] = (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f;
        }
        const std::vector<f32> fine = box_blur(light, w, h, kFineRadius);
        const std::vector<f32> broad = box_blur(light, w, h, kBroadRadius);
        std::vector<f32> detail(count);
        f64 squares = 0;
        for (usize i = 0; i < count; ++i)
        {
            detail[i] = fine[i] - broad[i];
            squares += static_cast<f64>(detail[i]) * detail[i];
        }
        const auto deviation = static_cast<f32>(std::sqrt(squares / static_cast<f64>(count)));
        std::vector<f32> height(count, 0.5f);
        if (deviation > kNoDetail)
        {
            for (usize i = 0; i < count; ++i)
                height[i] = std::clamp(0.5f + strength * detail[i] / (kSpread * deviation), 0.0f, 1.0f);
        }
        const std::vector<f32> around = box_blur(height, w, h, kHollowRadius);

        const auto at = [&](i32 x, i32 y) { return height[static_cast<usize>((((y % h) + h) % h) * w + ((x % w) + w) % w)]; };
        for (i32 y = 0; y < h; ++y)
        {
            for (i32 x = 0; x < w; ++x)
            {
                // Sobel: the height's slope along x and y, smoothed across.
                const f32 dx = (at(x + 1, y - 1) + 2 * at(x + 1, y) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2 * at(x - 1, y) - at(x - 1, y + 1)) / 8.0f;
                const f32 dy = (at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2 * at(x, y - 1) - at(x + 1, y - 1)) / 8.0f;
                const f32 nx = -dx * kSlope;
                const f32 ny = -dy * kSlope;
                const f32 length = std::sqrt(nx * nx + ny * ny + 1.0f);
                u8* out = &map.pixels[static_cast<usize>(y * w + x) * 4];
                out[0] = unit_byte(0.5f + 0.5f * nx / length);
                out[1] = unit_byte(0.5f + 0.5f * ny / length);
                const auto i = static_cast<usize>(y * w + x);
                out[2] = unit_byte(height[i]);
                out[3] = unit_byte(1.0f - kOcclusion * std::max(around[i] - height[i], 0.0f));
            }
        }
        return map;
    }

    void add_relief_maps(std::map<std::string, ovl::texture::RgbaImage, std::less<>>& pictures, f32 strength)
    {
        std::vector<std::pair<std::string, const ovl::texture::RgbaImage*>> wanted;
        for (const auto& [name, picture] : pictures)
        {
            if (!name.ends_with(kReliefSuffix) && !pictures.contains(name + std::string(kReliefSuffix)))
                wanted.emplace_back(name + std::string(kReliefSuffix), &picture);
        }
        // Each map on its own; a park loads a few hundred pictures, so they are shared out over the cores.
        std::vector<ovl::texture::RgbaImage> made(wanted.size());
        std::atomic<usize> next = 0;
        {
            std::vector<std::jthread> workers;
            const u32 cores = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
            for (u32 i = 0; i < std::min<usize>(cores, wanted.size()); ++i)
            {
                workers.emplace_back([&] {
                    for (usize j = next++; j < wanted.size(); j = next++)
                        made[j] = relief_map(*wanted[j].second, strength);
                });
            }
        }
        for (usize j = 0; j < wanted.size(); ++j)
            pictures.emplace(std::move(wanted[j].first), std::move(made[j]));
    }

} // namespace rct3::park
