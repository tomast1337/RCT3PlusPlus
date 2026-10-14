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
        const Rgb colour = flexi_colour(c);
        CHECK(flexi_shade(colour, kFlexiFullShade) == colour);
        for (std::size_t s = 1; s < kFlexiShades; ++s)
            CHECK(brightness(flexi_shade(colour, s)) <= brightness(flexi_shade(colour, s - 1)));
        CHECK(brightness(flexi_shade(colour, 0)) > brightness(colour));
    }
    const Rgb any { 12, 200, 99 }; // Not one of the 32
    CHECK(flexi_shade(any, kFlexiFullShade) == any);
    CHECK(flexi_shade(any, 1000) == flexi_shade(any, kFlexiShades - 1));
    CHECK(flexi_colour(kFlexiColourCount + 2) == flexi_colour(2));
}

TEST_CASE("stored colour picks name colours; out of range is the first")
{
    CHECK(flexi_colours({ 5, -1, 32 }) == FlexiColours { flexi_colour(5), flexi_colour(0), flexi_colour(0) });
    CHECK(flexi_colours({ 31, 0, 12 }) == FlexiColours { flexi_colour(31), flexi_colour(0), flexi_colour(12) });
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
    const Rgb orange { 250, 120, 10 };
    const ovl::RgbaImage painted = paint(*image, { Rgb { }, Rgb { }, orange });
    const Rgb shade = flexi_shade(orange, 200 - 1 - 2 * kFlexiShades);
    CHECK(painted.pixels == std::vector<std::uint8_t> { 255, 0, 0, 255, shade.r, shade.g, shade.b, 7 });

    // Without that slot the texture's own colour stays.
    const auto plain = palette_image(frame.view(false), 0b011);
    REQUIRE(plain);
    CHECK(paint(*plain, { orange, orange, orange }).pixels == std::vector<std::uint8_t> { 255, 0, 0, 255, 55, 0, 200, 255 });
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
    const FlexiColours colours { Rgb { 1, 2, 3 }, Rgb { 255, 16, 0 }, Rgb { 0xab, 0xcd, 0xef } };
    CHECK(painted_name("t", 0, colours) == "t");
    CHECK(painted_name("t", 0b101, colours) == "t#010203#000000#abcdef");
    CHECK(used_colours(0b010, colours) == FlexiColours { Rgb { }, Rgb { 255, 16, 0 }, Rgb { } });
}
