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

TEST_CASE("a car on an arm has its joints' parts apart, after every car, and each joint turns what it carries")
{
    ObjectModels models;
    ObjectModel model;
    ModelPart body = triangle("paint");
    ModelPart seat = triangle("paint");
    seat.bone = 1;
    model.levels = { { 100, { body, seat }, { } } };
    model.arm_joints = { glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), { 0, 2, 0 }) };
    models.emplace("Rides/Rides|Car", model);
    const std::vector objects { car(1), car(2) };
    const MovingMeshes m = moving_meshes(objects, models, { });
    // Each car's body, then each car's two joints, the first carrying nothing.
    CHECK(m.ends == std::vector<std::uint32_t> { 3, 6, 6, 9, 9, 12 });

    // The base turned a quarter about up, the next joint a quarter about sideways, about its own place.
    const std::vector<glm::mat4> moves = arm_moves(model.arm_joints, std::vector { 90.0f, 90.0f });
    REQUIRE(moves.size() == 2);
    const glm::vec3 above = moves[1] * glm::vec4(0, 3, 0, 1); // One above the second joint
    // Leant over towards z by the second joint, and that swung round to x by the base.
    CHECK(above.x == doctest::Approx(1));
    CHECK(above.y == doctest::Approx(2));
    CHECK(above.z == doctest::Approx(0).epsilon(1e-5));
    const glm::vec3 joint = moves[1] * glm::vec4(0, 2, 0, 1);
    CHECK(joint.y == doctest::Approx(2)); // A joint stays where it is
}

TEST_CASE("a model moved by bones keeps its triangles whole, each corner following its own bones by weight")
{
    ObjectModel model;
    ModelPart part = triangle("paint");
    part.vertices[0].bones = { 0, 1, 0, 0 }; // The second slot has no weight, so its bone means nothing
    part.vertices[0].weights = { 255, 0, 0, 0 };
    part.vertices[1].bones = { 1, 0, 0, 0 };
    part.vertices[1].weights = { 255, 0, 0, 0 };
    part.vertices[2].bones = { 0, 1, 0, 0 };
    part.vertices[2].weights = { 51, 204, 0, 0 };
    model.levels = { { 100, { part }, { } } };
    model.rig = BoneRig { .parents = { -1, 0 }, .rest_local = { }, .rest_world = { }, .motions = { } };
    auto [m, bones] = boned_meshes(car(1), model, { });
    CHECK(bones == std::vector<std::int32_t> { 0, 1 });
    REQUIRE(m.mesh.vertices.size() == 3);
    CHECK(m.mesh.indices.size() == 3); // One triangle, not cut apart by bone

    // The second bone lifted by 5, the first left where it is.
    move_meshes(m, std::vector { glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), { 0, 5, 0 }) });
    CHECK(m.mesh.vertices[0].position.y == doctest::Approx(0));
    CHECK(m.mesh.vertices[1].position.y == doctest::Approx(5));
    CHECK(m.mesh.vertices[2].position.y == doctest::Approx(4)); // Four fifths on the second bone
    CHECK(m.mesh.vertices[2].position.z == doctest::Approx(1));

    // The second bone turned a quarter about z: a normal shared half and half comes out unit length.
    move_meshes(m, std::vector { glm::mat4(1.0f), glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)) });
    CHECK(glm::length(m.mesh.vertices[2].normal) == doctest::Approx(1));
    CHECK(m.mesh.vertices[1].normal.x == doctest::Approx(-1));
}
