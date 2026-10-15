// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_models.hpp"

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
