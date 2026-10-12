// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/draw_list.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    constexpr Color kRed { 255, 0, 0, 255 };
    constexpr Color kBlue { 0, 0, 255, 255 };
} // namespace

TEST_CASE("a quad becomes four vertices and two triangles in screen order")
{
    DrawList list;
    list.image({ 10, 20, 30, 60 }, 7, { 0.25f, 0.5f, 0.75f, 1.0f }, kRed);
    REQUIRE(list.vertices().size() == 4);
    REQUIRE(list.indices().size() == 6);
    REQUIRE(list.batches().size() == 1);
    CHECK(list.batches()[0].texture == 7);
    CHECK(list.batches()[0].index_count == 6);

    const auto v = list.vertices();
    CHECK(v[0].x == 10);
    CHECK(v[0].y == 20);
    CHECK(v[0].u == 0.25f);
    CHECK(v[0].v == 0.5f);
    CHECK(v[2].x == 30);
    CHECK(v[2].y == 60);
    CHECK(v[2].u == 0.75f);
    CHECK(v[2].v == 1.0f);
    CHECK(v[1].color == std::array<std::uint8_t, 4> { 255, 0, 0, 255 });
}

TEST_CASE("consecutive quads with the same texture share a batch, others start a new one")
{
    DrawList list;
    list.fill({ 0, 0, 1, 1 }, kRed);
    list.fill({ 1, 0, 2, 1 }, kBlue);
    list.image({ 0, 0, 1, 1 }, 3, { 0, 0, 1, 1 });
    list.image({ 0, 0, 1, 1 }, 3, { 0, 0, 1, 1 });
    list.fill({ 0, 0, 1, 1 }, kRed);

    const auto b = list.batches();
    REQUIRE(b.size() == 3);
    CHECK(b[0].texture == 0);
    CHECK(b[0].first_index == 0);
    CHECK(b[0].index_count == 12);
    CHECK(b[1].texture == 3);
    CHECK(b[1].first_index == 12);
    CHECK(b[2].texture == 0);
    CHECK(b[2].first_index == 24);
    CHECK(list.indices()[24] == 16);
}

TEST_CASE("edges are rounded to whole pixels and empty rectangles are dropped")
{
    DrawList list;
    list.fill({ 0.4f, 0.6f, 10.5f, 3.2f }, kRed);
    list.fill({ 5, 5, 5, 9 }, kRed);
    list.fill({ 2.2f, 0, 2.4f, 4 }, kRed);
    REQUIRE(list.vertices().size() == 4);
    CHECK(list.vertices()[0].x == 0);
    CHECK(list.vertices()[0].y == 1);
    CHECK(list.vertices()[2].x == 11);
    CHECK(list.vertices()[2].y == 3);
}

TEST_CASE("clipping cuts geometry, texture coordinates and colours at the same place")
{
    DrawList list;
    REQUIRE(list.push_clip({ 5, 0, 100, 100 }));
    list.quad({ 0, 0, 10, 10 }, 1, { 0, 0, 1, 1 }, { kRed, kBlue, kBlue, kRed });
    list.pop_clip();

    const auto v = list.vertices();
    REQUIRE(v.size() == 4);
    CHECK(v[0].x == 5);
    CHECK(v[0].u == doctest::Approx(0.5f));
    CHECK(v[1].u == doctest::Approx(1.0f));
    CHECK(v[0].color == std::array<std::uint8_t, 4> { 128, 0, 128, 255 });
    CHECK(v[1].color == std::array<std::uint8_t, 4> { 0, 0, 255, 255 });
}

TEST_CASE("nested clips intersect and fully clipped drawing produces nothing")
{
    DrawList list;
    CHECK(list.push_clip({ 0, 0, 50, 50 }));
    CHECK(list.push_clip({ 40, 40, 80, 80 }));
    REQUIRE(list.clip() != nullptr);
    CHECK(*list.clip() == Rect { 40, 40, 50, 50 });
    CHECK_FALSE(list.push_clip({ 60, 60, 70, 70 }));
    list.fill({ 0, 0, 100, 100 }, kRed);
    CHECK(list.vertices().empty());
    list.pop_clip();
    list.fill({ 0, 0, 100, 100 }, kRed);
    CHECK(list.vertices().size() == 4);
    list.pop_clip();
    list.pop_clip();
    CHECK(list.clip() == nullptr);
}

TEST_CASE("clear starts the next frame empty")
{
    DrawList list;
    list.push_clip({ 0, 0, 1, 1 });
    list.fill({ 0, 0, 1, 1 }, kRed);
    list.clear();
    CHECK(list.vertices().empty());
    CHECK(list.indices().empty());
    CHECK(list.batches().empty());
    CHECK(list.clip() == nullptr);
}

TEST_CASE("the scale turns logical units into pixels before rounding")
{
    DrawList list;
    list.set_scale(2.5f);
    REQUIRE(list.push_clip({ 0, 0, 10, 10 }));
    CHECK(*list.clip() == Rect { 0, 0, 25, 25 });
    list.fill({ 1, 1, 3, 20 }, kRed);
    list.pop_clip();
    REQUIRE(list.vertices().size() == 4);
    CHECK(list.vertices()[0].x == 3); // 2.5 rounds up
    CHECK(list.vertices()[0].y == 3);
    CHECK(list.vertices()[2].x == 8); // 7.5 rounds up
    CHECK(list.vertices()[2].y == 25);
    list.clear();
    CHECK(list.scale() == 2.5f);
}
