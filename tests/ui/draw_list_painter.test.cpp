// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/draw_list_painter.hpp"

#include "ui/shape.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    constexpr Color kBlack { 0, 0, 0, 255 };

    Color at(const ovl::RgbaImage& image, std::uint32_t x, std::uint32_t y)
    {
        const std::uint8_t* p = &image.pixels[(std::size_t { y } * image.width + x) * 4];
        return { p[0], p[1], p[2], p[3] };
    }
} // namespace

TEST_CASE("a filled quad covers exactly the pixels inside it")
{
    DrawList list;
    list.fill({ 2, 1, 5, 3 }, { 255, 0, 0, 255 });
    auto picture = blank_picture(8, 4, kBlack);
    paint_draw_list(list, { }, picture);
    CHECK(at(picture, 2, 1) == Color { 255, 0, 0, 255 });
    CHECK(at(picture, 4, 2) == Color { 255, 0, 0, 255 });
    CHECK(at(picture, 1, 1) == kBlack);
    CHECK(at(picture, 5, 1) == kBlack);
    CHECK(at(picture, 2, 3) == kBlack);
}

TEST_CASE("half-clear quads blend once, also along the diagonal their triangles share")
{
    DrawList list;
    list.fill({ 0, 0, 6, 6 }, { 255, 255, 255, 128 });
    auto picture = blank_picture(6, 6, kBlack);
    paint_draw_list(list, { }, picture);
    for (std::uint32_t i = 0; i < 6; ++i)
        CHECK(at(picture, i, i) == Color { 128, 128, 128, 255 });
}

TEST_CASE("a textured quad samples its texture, tinted")
{
    // Two texels, red then blue, shown each over half of a quad four pixels wide.
    PainterTextures textures;
    textures.emplace(3, ovl::RgbaImage { 2, 1, { 255, 0, 0, 255, 0, 0, 255, 255 } });
    DrawList list;
    list.image({ 0, 0, 4, 1 }, 3, { 0, 0, 1, 1 }, { 255, 255, 255, 255 });
    list.image({ 0, 1, 4, 2 }, 3, { 0, 0, 1, 1 }, { 255, 255, 255, 128 });
    auto picture = blank_picture(4, 2, kBlack);
    paint_draw_list(list, textures, picture);
    CHECK(at(picture, 0, 0) == Color { 255, 0, 0, 255 });
    CHECK(at(picture, 3, 0) == Color { 0, 0, 255, 255 });
    // Between the texel centres, a mix.
    const Color middle = at(picture, 1, 0);
    CHECK(middle.r > 0);
    CHECK(middle.b > 0);
    CHECK(at(picture, 0, 1) == Color { 128, 0, 0, 255 });
}

TEST_CASE("a shape is shaded pixel by pixel, smooth at its edge")
{
    DrawList list;
    Shape disc;
    disc.rect = { 0, 0, 20, 20 };
    disc.corners = round_corners(10);
    list.shape(disc);
    auto picture = blank_picture(20, 20, kBlack);
    paint_draw_list(list, { }, picture);
    CHECK(at(picture, 10, 10) == kWhite);
    CHECK(at(picture, 0, 0) == kBlack);
    // A pixel the edge crosses is part covered.
    const Color edge = at(picture, 2, 3);
    CHECK(edge.r > 0);
    CHECK(edge.r < 255);
}
