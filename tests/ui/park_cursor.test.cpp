// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/park_cursor.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("over empty ground the pointer is the arrow, over something to open a hand")
{
    ParkPointer p;
    CHECK(park_cursor(p) == CursorKind::Arrow);
    p.over_pickable = true;
    CHECK(park_cursor(p) == CursorKind::Hand);
}

TEST_CASE("moving the camera by the mouse wins over everything else")
{
    ParkPointer p;
    p.interface = CursorKind::Text;
    p.over_pickable = true;
    p.tool = PointerTool::Delete;

    SUBCASE("turning hides the pointer")
    {
        p.turning = true;
        CHECK(park_cursor(p) == CursorKind::Hidden);
    }
    SUBCASE("dragging the ground holds it")
    {
        p.panning = true;
        CHECK(park_cursor(p) == CursorKind::Grabbing);
    }
}

TEST_CASE("the interface comes before zooming, tools and the park")
{
    ParkPointer p;
    p.interface = CursorKind::ResizeVertical;
    p.zooming = 1;
    p.tool = PointerTool::Land;
    p.over_pickable = true;
    CHECK(park_cursor(p) == CursorKind::ResizeVertical);
}

TEST_CASE("zooming from the keys shows which way")
{
    ParkPointer p;
    p.tool = PointerTool::Place;
    p.zooming = 1;
    CHECK(park_cursor(p) == CursorKind::ZoomIn);
    p.zooming = -1;
    CHECK(park_cursor(p) == CursorKind::ZoomOut);
}

TEST_CASE("each tool has its pointer, and not allowed where it cannot act")
{
    ParkPointer p;
    p.over_pickable = true;

    SUBCASE("placing holds the object")
    {
        p.tool = PointerTool::Place;
        CHECK(park_cursor(p) == CursorKind::Grab);
    }
    SUBCASE("the ground tools aim")
    {
        for (const PointerTool tool : { PointerTool::Delete, PointerTool::Land, PointerTool::Path, PointerTool::Water })
        {
            p.tool = tool;
            CHECK(park_cursor(p) == CursorKind::Crosshair);
        }
    }
    SUBCASE("blocked")
    {
        p.tool = PointerTool::Path;
        p.blocked = true;
        CHECK(park_cursor(p) == CursorKind::NotAllowed);
    }
}
