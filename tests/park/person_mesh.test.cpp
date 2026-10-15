// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/person_mesh.hpp"

#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("a person's colour picks a cell along the bottom row first, then up")
{
    CHECK(park::colour_cell(0) == glm::vec2(0.0f, 0.0f));
    CHECK(park::colour_cell(3) == glm::vec2(0.75f, 0.0f));
    CHECK(park::colour_cell(4) == glm::vec2(0.0f, -0.25f));
    CHECK(park::colour_cell(15) == glm::vec2(0.75f, -0.75f));
}

TEST_CASE("nobody to draw makes an empty mesh")
{
    const park::PersonLibrary library("/nonexistent", "Characters");
    const park::ObjectMesh mesh = park::person_mesh(library, { });
    CHECK(mesh.vertices.empty());
    CHECK(mesh.batches.empty());
}
