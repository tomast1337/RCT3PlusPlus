// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/relief_maps.hpp"

#include "core/types.hpp"

#include <cstdint>
#include <doctest/doctest.h>

using namespace rct3::park;
using rct3::ovl::texture::RgbaImage;

namespace
{
    constexpr u32 kSide = 16;

    /// Grey `base` with a column at x = 8 brighter by `lift`.
    RgbaImage ridge(u8 base, u8 lift)
    {
        RgbaImage picture { kSide, kSide, std::vector<u8>(kSide * kSide * 4, 255) };
        for (u32 y = 0; y < kSide; ++y)
        {
            for (u32 x = 0; x < kSide; ++x)
            {
                const auto v = static_cast<u8>(x == 8 ? base + lift : base);
                u8* p = &picture.pixels[(y * kSide + x) * 4];
                p[0] = p[1] = p[2] = v;
            }
        }
        return picture;
    }

    i32 texel(const RgbaImage& map, u32 x, u32 y, i32 channel)
    {
        return map.pixels[(y * map.width + x) * 4 + static_cast<u32>(channel)];
    }
} // namespace

TEST_CASE("a picture without detail gives a flat relief map")
{
    const RgbaImage map = relief_map(ridge(90, 0));
    REQUIRE(map.pixels.size() == kSide * kSide * 4);
    for (u32 i = 0; i < kSide * kSide; ++i)
    {
        CHECK(map.pixels[i * 4] == 128);
        CHECK(map.pixels[i * 4 + 1] == 128);
        CHECK(map.pixels[i * 4 + 2] == 128);
        CHECK(map.pixels[i * 4 + 3] == 255);
    }
}

TEST_CASE("a dark line is a hollow, closed to the sky; half strength makes it shallower")
{
    RgbaImage groove = ridge(150, 0);
    for (u32 y = 0; y < kSide; ++y)
        groove.pixels[(y * kSide + 8) * 4] = groove.pixels[(y * kSide + 8) * 4 + 1] = groove.pixels[(y * kSide + 8) * 4 + 2] = 40;
    const RgbaImage deep = relief_map(groove);
    const RgbaImage shallow = relief_map(groove, 0.5f);
    CHECK(texel(deep, 8, 4, 2) < texel(deep, 2, 4, 2));
    CHECK(texel(deep, 8, 4, 3) < 255 - 40);
    CHECK(texel(deep, 2, 4, 3) == 255);
    CHECK(texel(shallow, 8, 4, 2) > texel(deep, 8, 4, 2));
    CHECK(texel(shallow, 8, 4, 3) > texel(deep, 8, 4, 3));
}

TEST_CASE("a bright line is a ridge: high on top, its sides leaning away from it")
{
    const RgbaImage map = relief_map(ridge(90, 60));
    CHECK(texel(map, 8, 4, 2) > texel(map, 3, 4, 2));
    CHECK(texel(map, 7, 4, 0) < 128 - 20); // Left of the ridge, leaning to -x
    CHECK(texel(map, 9, 4, 0) > 128 + 20);
    CHECK(texel(map, 7, 4, 1) == 128); // No slope along y
}

TEST_CASE("faint detail is stretched to the same relief as strong detail")
{
    const RgbaImage faint = relief_map(ridge(100, 8));
    const RgbaImage strong = relief_map(ridge(60, 120));
    for (u32 x = 0; x < kSide; ++x)
        CHECK(std::abs(texel(faint, x, 0, 0) - texel(strong, x, 0, 0)) <= 2);
}

TEST_CASE("relief maps are added beside each picture once")
{
    std::map<std::string, RgbaImage, std::less<>> pictures { { "brick", ridge(90, 60) } };
    add_relief_maps(pictures);
    add_relief_maps(pictures);
    CHECK(pictures.size() == 2);
    CHECK(pictures.contains("brick#relief"));
}
