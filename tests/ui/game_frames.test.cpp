// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/draw_list_painter.hpp"
#include "ui/game_style.hpp"

#include <cstddef>
#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("each coded skin draws shapes alone, within the rectangle its picture would cover")
{
    for (const game_skins::CodedSkin& coded : game_skins::coded_skins())
    {
        CAPTURE(coded.skin);
        const Rect target { 20, 30, 220, 150 };
        DrawList list;
        draw_frame(list, coded.frame, target);
        CHECK(list.vertices().empty());
        REQUIRE_FALSE(list.shape_vertices().empty());
        for (const ShapeVertex& v : list.shape_vertices())
        {
            // Smoothing and soft shadows, grown with a frame drawn bigger than measured, reach a little past.
            CHECK(v.x >= target.x0 - 4);
            CHECK(v.x <= target.x1 + 4);
            CHECK(v.y >= target.y0 - 4);
            CHECK(v.y <= target.y1 + 4);
        }
    }
}

TEST_CASE("the game's look draws in code only the panels moved to code")
{
    SkinLibrary skins;
    game_skins::use_coded_skins(skins);
    for (const game_skins::CodedSkin& coded : game_skins::coded_skins())
    {
        CAPTURE(coded.skin);
        auto skin = skins.find(coded.skin);
        CHECK((skin && skin->coded) == (coded.look == PanelLook::Coded));
    }
}

TEST_CASE("the window frame leaves its middle empty for the window's background")
{
    DrawList list;
    draw_frame(list, game_frames::window(), { 0, 0, 300, 200 });
    // Shapes whose quads span the middle are outlines only: the middle point is outside every filled one.
    int filled_over_middle = 0;
    const auto vertices = list.shape_vertices();
    for (std::size_t i = 0; i + 3 < vertices.size(); i += 4)
    {
        const ShapeVertex& a = vertices[i];
        const ShapeVertex& c = vertices[i + 2];
        const bool spans = a.x < 150 && c.x > 150 && a.y < 120 && c.y > 120;
        const bool outline_only = a.top[3] == 0 && a.bottom[3] == 0;
        if (spans && !outline_only)
            ++filled_over_middle;
    }
    CHECK(filled_over_middle == 0);
}

namespace
{
    Color pixel(const rct3::ovl::RgbaImage& image, std::uint32_t x, std::uint32_t y)
    {
        const std::uint8_t* p = &image.pixels[(std::size_t { y } * image.width + x) * 4];
        return { p[0], p[1], p[2], p[3] };
    }
} // namespace

TEST_CASE("a layer cut along its diagonal keeps the half below it, however its box is stretched")
{
    CodedFrame frame;
    frame.width = 10;
    frame.height = 10;
    FrameLayer layer { corner_box(0, 0, 10, 10), Shape { }, { } };
    layer.shape.top = hex("#ff0000");
    layer.shape.bottom = hex("#ff0000");
    layer.cut_line = kBelowDiagonal;
    frame.layers.push_back(layer);
    for (const Rect target : { Rect { 0, 0, 40, 40 }, Rect { 0, 0, 80, 20 }, Rect { 0, 0, 16, 64 } })
    {
        CAPTURE(target.width());
        DrawList list;
        draw_frame(list, frame, target);
        const auto w = static_cast<std::uint32_t>(target.width());
        const auto h = static_cast<std::uint32_t>(target.height());
        auto picture = blank_picture(w, h, hex("#000000"));
        paint_draw_list(list, { }, picture);
        // Well below the line from the top-left to the bottom-right, and well above it.
        CHECK(pixel(picture, w / 8, h - h / 8 - 1).r == 255);
        CHECK(pixel(picture, w - w / 8 - 1, h / 8).r == 0);
        CHECK(pixel(picture, w / 2 - w / 8, h / 2 + h / 8).r == 255);
        CHECK(pixel(picture, w / 2 + w / 8, h / 2 - h / 8).r == 0);
    }
}

TEST_CASE("an arrow button's arrow points its way, with no seam down its middle")
{
    // A scroll arrow drawn at four times its size: the arrow's base 48 wide, 24 to its point.
    const Rect target { 0, 0, 60, 60 };
    const auto drawn = [&](game_frames::Arrow way) {
        DrawList list;
        draw_frame(list, game_frames::scroll_arrow(way), target);
        auto picture = blank_picture(60, 60, hex("#000000"));
        paint_draw_list(list, { }, picture);
        return picture;
    };
    const auto white = [](const auto& picture, std::uint32_t x, std::uint32_t y) { return pixel(picture, x, y).r == 255; };
    const auto up = drawn(game_frames::Arrow::Up);
    CHECK(white(up, 30, 30));
    CHECK(white(up, 30, 37)); // On the line the halves meet along
    CHECK(white(up, 12, 38));
    CHECK_FALSE(white(up, 12, 20)); // Beside the point
    CHECK_FALSE(white(up, 30, 12)); // Beyond the point
    const auto left = drawn(game_frames::Arrow::Left);
    CHECK(white(left, 30, 30));
    CHECK(white(left, 37, 12));
    CHECK_FALSE(white(left, 20, 12));
    CHECK_FALSE(white(left, 12, 30));
}

TEST_CASE("the front backdrop stays round on a wide screen and its strip reaches the right edge")
{
    // A design drawn at a fifth of its size on a screen far wider than it.
    constexpr float kScale = 0.2f;
    const Rect screen { 0, 0, 400, 768 * kScale };
    const Rect design_screen { 0, 0, screen.x1 / kScale, 768 };
    DrawList list;
    draw_frame(list, game_frames::front_backdrop(design_screen), { 0, 0, 1024 * kScale, 768 * kScale });
    for (const ShapeVertex& v : list.shape_vertices())
    {
        if (v.edge[3] > 0) // Rings are round
            CHECK(v.local[2] == doctest::Approx(v.local[3]));
    }
    auto picture = blank_picture(400, static_cast<std::uint32_t>(screen.y1), hex("#000000"));
    paint_draw_list(list, { }, picture);
    const Color right = pixel(picture, 399, 150);
    CHECK(right.b > 100);                // The strip's blue, at the far right
    CHECK(pixel(picture, 2, 2).b > 100); // Blue in the top-left corner
}

TEST_CASE("each piece of the front backdrop is the whole backdrop cut to that piece")
{
    const CodedFrame whole = game_frames::front_backdrop({ 0, 0, 1024, 768 });
    const CodedFrame piece = game_frames::front_piece(whole, { 512, 513, 1024, 768 });
    CHECK(piece.width == doctest::Approx(512));
    CHECK(piece.height == doctest::Approx(255));
    for (const FrameLayer& layer : piece.layers)
    {
        REQUIRE(layer.clip);
        CHECK(layer.clip->x0.offset >= 0);
        CHECK(layer.clip->y1.offset <= 255);
    }
}
