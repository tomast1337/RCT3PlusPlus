// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/flexi_colours.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    int brightness(Rgb c)
    {
        return c.r + c.g + c.b;
    }

    /// A 2 by 1 frame: palette entry i is (i, i, i) stored blue, green, red, unused.
    struct Frame
    {
        std::vector<std::byte> palette = std::vector<std::byte>(ovl::kFlexiPaletteSize);
        std::vector<std::byte> pixels { std::byte { 0 }, std::byte { 200 } };
        std::vector<std::byte> alpha { std::byte { 255 }, std::byte { 7 } };

        Frame()
        {
            for (std::size_t i = 0; i < 256; ++i)
            {
                palette[i * 4 + 0] = std::byte(i); // Blue
                palette[i * 4 + 1] = std::byte(0);
                palette[i * 4 + 2] = std::byte(255 - i); // Red
            }
        }

        ovl::FlexiFrame view(bool with_alpha) const
        {
            ovl::FlexiFrame f;
            f.width = 2;
            f.height = 1;
            f.palette = palette;
            f.pixels = pixels;
            if (with_alpha)
                f.alpha = std::span<const std::byte>(alpha);
            return f;
        }
    };
} // namespace

TEST_CASE("a colour's shades run from pale through the colour to dark")
{
    for (std::size_t c = 0; c < kFlexiColourCount; ++c)
    {
        CHECK(flexi_shade(c, kFlexiFullShade) == flexi_colour(c));
        for (std::size_t s = 1; s < kFlexiShades; ++s)
            CHECK(brightness(flexi_shade(c, s)) <= brightness(flexi_shade(c, s - 1)));
        CHECK(brightness(flexi_shade(c, 0)) > brightness(flexi_colour(c)));
    }
    CHECK(flexi_shade(3, 1000) == flexi_shade(3, kFlexiShades - 1));
    CHECK(flexi_colour(kFlexiColourCount + 2) == flexi_colour(2));
}

TEST_CASE("stored colour picks out of range become the first colour")
{
    CHECK(flexi_colours({ 5, -1, 32 }) == FlexiColours { 5, 0, 0 });
    CHECK(flexi_colours({ 31, 0, 12 }) == FlexiColours { 31, 0, 12 });
}

TEST_CASE("frames keep their indices and are painted per slot")
{
    const Frame frame;
    const auto image = palette_image(frame.view(true), 0b1111);
    REQUIRE(image);
    CHECK(image->slots == 0b111);
    CHECK(image->palette[200] == Rgb { 55, 0, 200 });
    CHECK(image->indices == std::vector<std::uint8_t> { 0, 200 });
    CHECK(image->alpha == std::vector<std::uint8_t> { 255, 7 });

    // Index 200 lies in the third slot's run, at shade 200 - 171.
    const ovl::RgbaImage painted = paint(*image, { 0, 0, 17 });
    const Rgb shade = flexi_shade(17, 200 - 1 - 2 * kFlexiShades);
    CHECK(painted.pixels == std::vector<std::uint8_t> { 255, 0, 0, 255, shade.r, shade.g, shade.b, 7 });

    // Without that slot the texture's own colour stays.
    const auto plain = palette_image(frame.view(false), 0b011);
    REQUIRE(plain);
    CHECK(paint(*plain, { 4, 4, 17 }).pixels == std::vector<std::uint8_t> { 255, 0, 0, 255, 55, 0, 200, 255 });
}

TEST_CASE("malformed frames are refused")
{
    const Frame frame;
    ovl::FlexiFrame f = frame.view(true);
    f.pixels = f.pixels.first(1);
    CHECK_FALSE(palette_image(f, 7));
    f = frame.view(true);
    f.width = 0;
    CHECK_FALSE(palette_image(f, 7));
    f = frame.view(true);
    f.palette = f.palette.first(16);
    CHECK_FALSE(palette_image(f, 7));
}

TEST_CASE("painted names tell colourings apart only by the slots used")
{
    CHECK(painted_name("t", 0, { 1, 2, 3 }) == "t");
    CHECK(painted_name("t", 0b101, { 1, 2, 3 }) == "t#1,0,3");
    CHECK(used_colours(0b010, { 1, 2, 3 }) == FlexiColours { 0, 2, 0 });
}
