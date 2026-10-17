// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_models.hpp"

#include <algorithm>
#include <cstdlib>
#include <doctest/doctest.h>

using namespace rct3::park;

TEST_CASE("a model takes the colour slots of all its pictures, at every level")
{
    ObjectModel model;
    CHECK(colour_slots(model) == 0);
    ModelLevel& near = model.levels.emplace_back();
    near.parts.emplace_back().slots = 0b001;
    near.parts.emplace_back().slots = 0;
    ModelLevel& far = model.levels.emplace_back();
    far.billboard = Billboard { };
    far.billboard->slots = 0b100;
    CHECK(colour_slots(model) == 0b101);
}

TEST_CASE("the water picture is see-through, tiles and is never recoloured")
{
    const PaletteImage water = water_picture();
    REQUIRE(water.width > 0);
    REQUIRE(water.indices.size() == water.width * water.height);
    REQUIRE(water.alpha.size() == water.indices.size());
    CHECK(water.slots == 0);
    CHECK(std::ranges::all_of(water.alpha, [](std::uint8_t a) { return a > 0 && a < 255; }));
    // The edges meet: across the seam the shade steps no more than between neighbours inside the picture.
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return static_cast<int>(water.indices[y * water.width + x]); };
    int inside = 0;
    int seam = 0;
    for (std::uint32_t y = 0; y < water.height; ++y)
    {
        for (std::uint32_t x = 1; x < water.width; ++x)
            inside = std::max(inside, std::abs(at(x, y) - at(x - 1, y)));
        seam = std::max(seam, std::abs(at(0, y) - at(water.width - 1, y)));
    }
    CHECK(seam <= inside);
}
