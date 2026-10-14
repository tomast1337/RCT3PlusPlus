// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_textures.hpp"

#include <doctest/doctest.h>

using namespace rct3::park;

TEST_CASE("cliff faces are told from land surfaces by their name")
{
    CHECK(ground_paint("Cliff_03") == GroundPaint::Cliff);
    CHECK(ground_paint("Terrain_07") == GroundPaint::Surface);
    CHECK(ground_paint("Sand") == GroundPaint::Surface);
    CHECK(ground_key(GroundPaint::Cliff, 3) != ground_key(GroundPaint::Surface, 3));
}

TEST_CASE("an overlay that is not there is an error")
{
    GroundImages images;
    CHECK_FALSE(add_ground_images(images, "no such overlay.ovl").has_value());
    CHECK(images.empty());
}
