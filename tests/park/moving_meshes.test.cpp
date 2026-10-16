// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/moving_meshes.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    ModelPart triangle(std::string texture)
    {
        ModelPart p;
        p.texture = std::move(texture);
        p.vertices = { { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0 } }, { { 1, 0, 0 }, { 0, 1, 0 }, { 1, 0 } }, { { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1 } } };
        p.indices = { 0, 1, 2 };
        return p;
    }

    save::PlacedObject car(std::uint64_t id)
    {
        save::PlacedObject o;
        o.id = id;
        o.owner_ref = id;
        o.symbol = "Car";
        o.overlay = "Rides/Rides";
        return o;
    }
} // namespace

TEST_CASE("moving shapes are built once, drawn alike in one batch, and then only moved")
{
    ObjectModels models;
    ObjectModel model;
    model.levels = { { 100, { triangle("paint") }, { } } };
    models.emplace("Rides/Rides|Car", model);
    const std::vector objects { car(1), car(2) };
    MovingMeshes m = moving_meshes(objects, models, { });
    REQUIRE(m.mesh.batches.size() == 1);
    CHECK(m.mesh.batches[0].count == 6);
    CHECK(m.mesh.vertices.size() == 6);
    CHECK(m.ends == std::vector<std::uint32_t> { 3, 6 });

    // The second car moved to (10, 0, 0) and turned a quarter about y: its +x edge now points to -z.
    const std::vector places { glm::mat4(1.0f), glm::rotate(glm::translate(glm::mat4(1.0f), { 10, 0, 0 }), glm::radians(90.0f), glm::vec3(0, 1, 0)) };
    move_meshes(m, places);
    const glm::vec3 edge = m.mesh.vertices[4].position;
    CHECK(edge.x == doctest::Approx(10));
    CHECK(edge.z == doctest::Approx(-1));
    CHECK(m.mesh.vertices[1].position.x == doctest::Approx(1)); // The first stays put
    CHECK(m.mesh.vertices[4].normal.y == doctest::Approx(1));
    CHECK(m.mesh.indices[3] == 3); // The second car's triangle uses its own vertices
}
