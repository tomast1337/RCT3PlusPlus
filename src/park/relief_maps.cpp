// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/relief_maps.hpp"

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
        constexpr int kFineRadius = 1;     ///< Smooths texel noise before it is read as height
        constexpr int kBroadRadius = 6;    ///< Shading broader than this is taken as colour, not height
        constexpr float kSpread = 4.0f;    ///< Fine detail this many standard deviations across fills the height's range
        constexpr float kSlope = 2.0f;     ///< How much a step in height leans the surface
        constexpr int kHollowRadius = 3;   ///< A point lower than its surroundings this far round is in a hollow
        constexpr float kOcclusion = 0.7f; ///< How much of the sky a hollow as deep as the height's range hides
        constexpr float kNoDetail = 1e-4f; ///< A picture whose detail varies less than this is flat

        /// `count` values from `in`, `stride` apart, averaged over `radius` values each side, wrapping round, into
        /// `out` at the same places: a running sum, so the cost does not grow with the radius.
        void blur_line(const float* in, float* out, int count, int stride, int radius)
        {
            const auto at = [&](int i) { return in[static_cast<std::ptrdiff_t>((((i % count) + count) % count) * stride)]; };
            const float scale = 1.0f / static_cast<float>(2 * radius + 1);
            float sum = 0;
            for (int d = -radius; d <= radius; ++d)
                sum += at(d);
            for (int i = 0; i < count; ++i)
            {
                out[static_cast<std::ptrdiff_t>(i * stride)] = sum * scale;
                sum += at(i + radius + 1) - at(i - radius);
            }
        }

        /// `values` (w by h) averaged over a square `radius` texels each side, wrapping round.
        std::vector<float> box_blur(const std::vector<float>& values, int w, int h, int radius)
        {
            std::vector<float> across(values.size());
            for (int y = 0; y < h; ++y)
                blur_line(&values[static_cast<std::size_t>(y * w)], &across[static_cast<std::size_t>(y * w)], w, 1, radius);
            std::vector<float> out(values.size());
            for (int x = 0; x < w; ++x)
                blur_line(&across[static_cast<std::size_t>(x)], &out[static_cast<std::size_t>(x)], h, w, radius);
            return out;
        }

        std::uint8_t unit_byte(float v)
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        }
    } // namespace

    ovl::RgbaImage relief_map(const ovl::RgbaImage& picture, float strength)
    {
        const int w = static_cast<int>(picture.width);
        const int h = static_cast<int>(picture.height);
        const auto count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        ovl::RgbaImage map { picture.width, picture.height, std::vector<std::uint8_t>(count * 4) };
        if (count == 0 || picture.pixels.size() < count * 4)
            return map;

        std::vector<float> light(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint8_t* p = &picture.pixels[i * 4];
            light[i] = (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f;
        }
        const std::vector<float> fine = box_blur(light, w, h, kFineRadius);
        const std::vector<float> broad = box_blur(light, w, h, kBroadRadius);
        std::vector<float> detail(count);
        double squares = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            detail[i] = fine[i] - broad[i];
            squares += static_cast<double>(detail[i]) * detail[i];
        }
        const auto deviation = static_cast<float>(std::sqrt(squares / static_cast<double>(count)));
        std::vector<float> height(count, 0.5f);
        if (deviation > kNoDetail)
        {
            for (std::size_t i = 0; i < count; ++i)
                height[i] = std::clamp(0.5f + strength * detail[i] / (kSpread * deviation), 0.0f, 1.0f);
        }
        const std::vector<float> around = box_blur(height, w, h, kHollowRadius);

        const auto at = [&](int x, int y) { return height[static_cast<std::size_t>((((y % h) + h) % h) * w + ((x % w) + w) % w)]; };
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                // Sobel: the height's slope along x and y, smoothed across.
                const float dx = (at(x + 1, y - 1) + 2 * at(x + 1, y) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2 * at(x - 1, y) - at(x - 1, y + 1)) / 8.0f;
                const float dy = (at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2 * at(x, y - 1) - at(x + 1, y - 1)) / 8.0f;
                const float nx = -dx * kSlope;
                const float ny = -dy * kSlope;
                const float length = std::sqrt(nx * nx + ny * ny + 1.0f);
                std::uint8_t* out = &map.pixels[static_cast<std::size_t>(y * w + x) * 4];
                out[0] = unit_byte(0.5f + 0.5f * nx / length);
                out[1] = unit_byte(0.5f + 0.5f * ny / length);
                const auto i = static_cast<std::size_t>(y * w + x);
                out[2] = unit_byte(height[i]);
                out[3] = unit_byte(1.0f - kOcclusion * std::max(around[i] - height[i], 0.0f));
            }
        }
        return map;
    }

    void add_relief_maps(std::map<std::string, ovl::RgbaImage, std::less<>>& pictures, float strength)
    {
        std::vector<std::pair<std::string, const ovl::RgbaImage*>> wanted;
        for (const auto& [name, picture] : pictures)
        {
            if (!name.ends_with(kReliefSuffix) && !pictures.contains(name + std::string(kReliefSuffix)))
                wanted.emplace_back(name + std::string(kReliefSuffix), &picture);
        }
        // Each map on its own; a park loads a few hundred pictures, so they are shared out over the cores.
        std::vector<ovl::RgbaImage> made(wanted.size());
        std::atomic<std::size_t> next = 0;
        {
            std::vector<std::jthread> workers;
            const unsigned cores = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
            for (unsigned i = 0; i < std::min<std::size_t>(cores, wanted.size()); ++i)
            {
                workers.emplace_back([&] {
                    for (std::size_t j = next++; j < wanted.size(); j = next++)
                        made[j] = relief_map(*wanted[j].second, strength);
                });
            }
        }
        for (std::size_t j = 0; j < wanted.size(); ++j)
            pictures.emplace(std::move(wanted[j].first), std::move(made[j]));
    }

} // namespace rct3::park
