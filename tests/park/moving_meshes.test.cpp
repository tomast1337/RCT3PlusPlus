// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/moving_meshes.hpp"

#include "core/types.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    object::ModelPart triangle(std::string texture)
    {
        object::ModelPart p;
        p.texture = std::move(texture);
        p.vertices = { { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0 } }, { { 1, 0, 0 }, { 0, 1, 0 }, { 1, 0 } }, { { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1 } } };
        p.indices = { 0, 1, 2 };
        return p;
    }

    save::model::PlacedObject car(u64 id)
    {
        save::model::PlacedObject o;
        o.id = id;
        o.owner_ref = id;
        o.symbol = "Car";
        o.overlay = "Rides/Rides";
        return o;
    }
} // namespace

TEST_CASE("moving shapes are built once, drawn alike in one batch, and then only moved")
{
    object::ObjectModels models;
    object::ObjectModel model;
    model.levels = { { 100, { triangle("paint") }, { } } };
    models.emplace("Rides/Rides|Car", model);
    const std::vector objects { car(1), car(2) };
    MovingMeshes m = moving_meshes(objects, models, { });
    REQUIRE(m.mesh.batches.size() == 1);
    CHECK(m.mesh.batches[0].count == 6);
    CHECK(m.mesh.vertices.size() == 6);
    CHECK(m.ends == std::vector<u32> { 3, 6 });

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
    object::ObjectModels models;
    object::ObjectModel model;
    object::ModelPart body = triangle("paint");
    object::ModelPart seat = triangle("paint");
    seat.bone = 1;
    model.levels = { { 100, { body, seat }, { } } };
    model.arm_joints = { glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), { 0, 2, 0 }) };
    models.emplace("Rides/Rides|Car", model);
    const std::vector objects { car(1), car(2) };
    const MovingMeshes m = moving_meshes(objects, models, { });
    // Each car's body, then each car's two joints, the first carrying nothing.
    CHECK(m.ends == std::vector<u32> { 3, 6, 6, 9, 9, 12 });

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
    object::ObjectModel model;
    object::ModelPart part = triangle("paint");
    part.vertices[0].bones = { 0, 1, 0, 0 }; // The second slot has no weight, so its bone means nothing
    part.vertices[0].weights = { 255, 0, 0, 0 };
    part.vertices[1].bones = { 1, 0, 0, 0 };
    part.vertices[1].weights = { 255, 0, 0, 0 };
    part.vertices[2].bones = { 0, 1, 0, 0 };
    part.vertices[2].weights = { 51, 204, 0, 0 };
    model.levels = { { 100, { part }, { } } };
    model.rig = BoneRig { .parents = { std::nullopt, ovl::BoneIndex { 0 } }, .rest_local = { }, .rest_world = { }, .motions = { } };
    auto [m, bones] = boned_meshes(car(1), model, { });
    CHECK(bones == std::vector<i32> { 0, 1 });
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

TEST_CASE("a car's part held level stays level and on its pivot as its car pitches, over the top too, heading as the car does")
{
    // Hung from a pivot a metre over the car's origin, turning about the pivot's sideways line.
    const object::CarPart part { .moves = { }, .pivot = glm::translate(glm::mat4(1.0f), { 0, 1, 0 }), .axis = { 1, 0, 0 }, .up = { 0, 1, 0 } };
    constexpr f32 kHeading = 0.7f;
    // Past a quarter turn either way the car is upside down, as over the top of a loop.
    for (const f32 pitch : { -2.6f, -1.4f, -0.5f, 0.0f, 0.3f, 1.2f, 2.0f, glm::pi<f32>(), 4.0f })
    {
        CAPTURE(pitch);
        const glm::mat4 place = glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0f), { 5, 2, 3 }), kHeading, { 0, 1, 0 }), pitch, { 1, 0, 0 });
        const glm::mat4 drawn = place * part_move(part, place);
        const glm::vec3 up = glm::mat3(drawn) * glm::vec3(0, 1, 0);
        CHECK(up.x == doctest::Approx(0).epsilon(1e-5));
        CHECK(up.y == doctest::Approx(1));
        CHECK(up.z == doctest::Approx(0).epsilon(1e-5));
        const glm::vec3 ahead = glm::mat3(drawn) * glm::vec3(0, 0, 1);
        CHECK(ahead.x == doctest::Approx(std::sin(kHeading)));
        CHECK(ahead.z == doctest::Approx(std::cos(kHeading)));
        const glm::vec3 pivot = drawn * glm::vec4(0, 1, 0, 1);
        const glm::vec3 on_car = place * glm::vec4(0, 1, 0, 1);
        CHECK(glm::distance(pivot, on_car) == doctest::Approx(0).epsilon(1e-5));
    }
}

TEST_CASE("a car's part held level turns only about its own line: rolled, the car takes it over with it")
{
    const object::CarPart part { .moves = { }, .pivot = glm::mat4(1.0f), .axis = { 1, 0, 0 }, .up = { 0, 1, 0 } };
    const glm::mat4 rolled = glm::rotate(glm::mat4(1.0f), 0.4f, { 0, 0, 1 });
    CHECK(part_move(part, rolled) == glm::mat4(1.0f));
    // Out of sight, a car stands nowhere, and its part stays at rest.
    CHECK(part_move(part, glm::mat4(0.0f)) == glm::mat4(1.0f));
}

TEST_CASE("a part held level on a car riding on its side stays upright up, over the top and down, its pivot a quarter turned")
{
    // The car's pivot is turned a quarter about the car's forward from the part's: its sideways line is the car's
    // down, and the part's up (the pivot's own up) is the car's left (-X). The car rides on its side, its left up.
    const glm::mat4 pivot { glm::vec4(0, -1, 0, 0), glm::vec4(-1, 0, 0, 0), glm::vec4(0, 0, -1, 0), glm::vec4(0, 0.5f, 0, 1) };
    const object::CarPart part { .moves = { }, .pivot = pivot, .axis = { 1, 0, 0 }, .up = { 0, 1, 0 } };
    const glm::mat4 on_side = glm::rotate(glm::mat4(1.0f), -glm::half_pi<f32>(), { 0, 0, 1 });
    // Past a quarter turn it goes over the top and back down, as on a ride over the top of a tower.
    for (const f32 climb : { -2.5f, -1.2f, -0.3f, 0.0f, 0.6f, 1.5f, 2.2f, glm::pi<f32>(), 4.0f })
    {
        CAPTURE(climb);
        const glm::mat4 place = glm::rotate(glm::mat4(1.0f), -climb, { 1, 0, 0 }) * on_side;
        const glm::mat3 drawn(place * part_move(part, place) * pivot);
        const glm::vec3 up = drawn * part.up;
        CHECK(up.x == doctest::Approx(0).epsilon(1e-5));
        CHECK(up.y == doctest::Approx(1));
        CHECK(up.z == doctest::Approx(0).epsilon(1e-5));
        // It keeps the way it faces at the station, the car's back, upside down or not.
        const glm::vec3 ahead = drawn * glm::vec3(0, 0, 1);
        CHECK(ahead.x == doctest::Approx(0).epsilon(1e-5));
        CHECK(ahead.y == doctest::Approx(0).epsilon(1e-5));
        CHECK(ahead.z == doctest::Approx(-1));
    }
}

TEST_CASE("a car's swinging part is drawn swung from level by its angle, about its line, on its pivot")
{
    // Hung from a pivot a metre over the car's origin, swinging about the car's forward line, the car banked.
    const object::CarPart part { .moves = { }, .pivot = glm::translate(glm::mat4(1.0f), { 0, 1, 0 }), .axis = { 0, 0, 1 }, .up = { 0, 1, 0 } };
    const glm::mat4 banked = glm::rotate(glm::translate(glm::mat4(1.0f), { 5, 2, 3 }), 0.3f, { 0, 0, 1 });
    for (const f32 swung : { -0.6f, 0.0f, 0.4f })
    {
        CAPTURE(swung);
        const glm::mat4 drawn = banked * part_move(part, banked, units::Radians { swung });
        const glm::vec3 up = glm::mat3(drawn) * glm::vec3(0, 1, 0);
        CHECK(up.x == doctest::Approx(-std::sin(swung)).epsilon(1e-5));
        CHECK(up.y == doctest::Approx(std::cos(swung)));
        const glm::vec3 pivot = drawn * glm::vec4(0, 1, 0, 1);
        CHECK(glm::distance(pivot, glm::vec3(banked * glm::vec4(0, 1, 0, 1))) == doctest::Approx(0).epsilon(1e-5));
    }
    // Sprung to its car, it rests banked with the car and swings from there.
    object::CarPart sprung = part;
    sprung.moves.spring = 100;
    CHECK(part_move(sprung, banked) == glm::mat4(1.0f));
}
