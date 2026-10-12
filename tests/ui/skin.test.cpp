// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/skin.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    ovl::GuiSkin skin(ovl::GuiSkinKind kind, std::vector<std::int32_t> coordinates)
    {
        return { static_cast<std::uint32_t>(kind), "atlas", std::move(coordinates) };
    }
} // namespace

TEST_CASE("skin coordinates split into lines per kind")
{
    SUBCASE("rect lists two corners")
    {
        const auto layout = skin_layout(skin(ovl::GuiSkinKind::Rect, { 4, 8, 20, 30 }));
        REQUIRE(layout);
        CHECK(layout->x_count == 2);
        CHECK(layout->xs[0] == 4);
        CHECK(layout->xs[1] == 20);
        CHECK(layout->ys[0] == 8);
        CHECK(layout->ys[1] == 30);
    }
    SUBCASE("three-cell kinds have four lines on their long axis")
    {
        const auto h = skin_layout(skin(ovl::GuiSkinKind::HorizontalThree, { 0, 2, 10, 12, 5, 9 }));
        REQUIRE(h);
        CHECK(h->x_count == 4);
        CHECK(h->y_count == 2);
        const auto v = skin_layout(skin(ovl::GuiSkinKind::VerticalThree, { 5, 9, 0, 2, 10, 12 }));
        REQUIRE(v);
        CHECK(v->x_count == 2);
        CHECK(v->y_count == 4);
    }
    SUBCASE("bad input is refused")
    {
        CHECK_FALSE(skin_layout(skin(ovl::GuiSkinKind::NinePatch, { 0, 1, 2, 3 })));
        CHECK_FALSE(skin_layout(skin(ovl::GuiSkinKind::Rect, { 10, 0, 5, 4 })));
        CHECK_FALSE(skin_layout({ 77, "atlas", { } }));
    }
}

TEST_CASE("a nine-patch keeps its corners and stretches the rest")
{
    const auto layout = skin_layout(skin(ovl::GuiSkinKind::NinePatch, { 0, 4, 12, 16, 0, 4, 12, 16 }));
    REQUIRE(layout);
    const auto pieces = skin_pieces(*layout, { 100, 200, 200, 260 }, 64, 32);
    REQUIRE(pieces.count == 9);
    const auto p = pieces.pieces();
    CHECK(p[0].target == Rect { 100, 200, 104, 204 });
    CHECK(p[4].target == Rect { 104, 204, 196, 256 });
    CHECK(p[8].target == Rect { 196, 256, 200, 260 });
    CHECK(p[4].uv.x0 == doctest::Approx(4.0f / 64));
    CHECK(p[4].uv.y1 == doctest::Approx(12.0f / 32));
}

TEST_CASE("a frame leaves the centre out")
{
    const auto layout = skin_layout(skin(ovl::GuiSkinKind::Frame, { 0, 4, 12, 16, 0, 4, 12, 16 }));
    REQUIRE(layout);
    const auto pieces = skin_pieces(*layout, { 0, 0, 50, 50 }, 16, 16);
    REQUIRE(pieces.count == 8);
    for (const SkinPiece& piece : pieces.pieces())
        CHECK_FALSE(piece.target == Rect { 4, 4, 46, 46 });
}

TEST_CASE("caps shrink in proportion when the target is too small")
{
    const auto layout = skin_layout(skin(ovl::GuiSkinKind::HorizontalThree, { 0, 6, 10, 12, 0, 8 }));
    REQUIRE(layout);
    const auto pieces = skin_pieces(*layout, { 0, 0, 4, 8 }, 16, 8);
    const auto p = pieces.pieces();
    REQUIRE(p.size() == 3);
    CHECK(p[0].target == Rect { 0, 0, 3, 8 });
    CHECK(p[1].target.width() == 0);
    CHECK(p[2].target == Rect { 3, 0, 4, 8 });
}

TEST_CASE("solid skins and empty textures draw nothing")
{
    const auto solid = skin_layout(skin(ovl::GuiSkinKind::Solid, { }));
    REQUIRE(solid);
    CHECK(skin_pieces(*solid, { 0, 0, 10, 10 }, 16, 16).count == 0);
    const auto rect = skin_layout(skin(ovl::GuiSkinKind::Rect, { 0, 0, 4, 4 }));
    REQUIRE(rect);
    CHECK(skin_pieces(*rect, { 0, 0, 10, 10 }, 0, 16).count == 0);
}

TEST_CASE("flipping mirrors y lines about the texture height")
{
    const auto layout = skin_layout(skin(ovl::GuiSkinKind::VerticalThree, { 0, 8, 2, 4, 10, 20 }));
    REQUIRE(layout);
    const SkinLayout flipped = flipped_vertically(*layout, 32);
    CHECK(flipped.ys[0] == 12);
    CHECK(flipped.ys[1] == 22);
    CHECK(flipped.ys[2] == 28);
    CHECK(flipped.ys[3] == 30);
}

TEST_CASE("draw_skin adds one quad per visible piece")
{
    const auto layout = skin_layout(skin(ovl::GuiSkinKind::NinePatch, { 0, 4, 12, 16, 0, 4, 12, 16 }));
    REQUIRE(layout);
    DrawList list;
    draw_skin(list, *layout, { 0, 0, 40, 40 }, 9, 16, 16);
    CHECK(list.vertices().size() == 36);
    REQUIRE(list.batches().size() == 1);
    CHECK(list.batches()[0].texture == 9);
}
