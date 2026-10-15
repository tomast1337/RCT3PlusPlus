// SPDX-License-Identifier: GPL-3.0-or-later
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
        const SkinRef* skin = skins.find(coded.skin);
        CHECK((skin != nullptr && skin->coded != nullptr) == (coded.look == PanelLook::Coded));
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
