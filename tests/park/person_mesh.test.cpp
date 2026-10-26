// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/person_mesh.hpp"

#include "park/person_motions.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <string_view>

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
    const std::array<std::string_view, 1> folders { "Characters" };
    const park::PersonLibrary library("/nonexistent", folders);
    park::PersonRuns runs;
    const park::ObjectMesh mesh = park::person_mesh(library, { }, runs);
    CHECK(mesh.vertices.empty());
    CHECK(mesh.batches.empty());
}

TEST_CASE("groups of people carried together are each one piece, standing where their carrier puts them")
{
    const std::array<std::string_view, 1> folders { "Characters" };
    const park::PersonLibrary library("/nonexistent", folders);
    const std::array<park::PlacedPerson, 3> riders { };
    const std::array<std::span<const park::PlacedPerson>, 2> cars { std::span(riders).first(1), std::span(riders).subspan(1) };
    park::PersonRuns runs, gathered;
    const park::MovingMeshes meshes = park::carried_people_meshes(library, cars, runs, gathered);
    CHECK(meshes.ends.size() == 2);
    CHECK(meshes.places.size() == 2);
    CHECK(meshes.mesh.placed == 3);
    CHECK(meshes.mesh.batches.empty()); // A library without models draws nobody
}

TEST_CASE("a hand's socket is at the middle of its triangle as the motion poses it")
{
    // One key: three points a step of 0.01 apart, packed eleven bits across, ten up, eleven along.
    park::PieceModel socket;
    socket.left_hand = true;
    socket.mesh.points = 3;
    socket.mesh.normals = 1;
    socket.mesh.tracks = 1;
    for (std::uint16_t p = 0; p < 3; ++p)
        socket.mesh.corners.push_back({ p, 0, { } });
    socket.mesh.indices = { 0, 1, 2 };
    ovl::FrameAnimation motion;
    motion.scale = { 0.01f, 0.01f, 0.01f };
    motion.times = { 1.0f };
    const std::array<std::array<std::uint32_t, 3>, 3> steps { { { 10, 100, 20 }, { 13, 100, 20 }, { 10, 103, 23 } } };
    for (const auto& [x, y, z] : steps)
    {
        const std::uint32_t word = x << 21 | y << 11 | z;
        for (int b = 0; b < 4; ++b)
            motion.keys.push_back(static_cast<std::byte>(word >> (8 * b)));
    }
    motion.keys.resize(socket.mesh.key_size(), std::byte { 127 });
    socket.mesh.animations.push_back(motion);
    const auto hand = park::socket_point(socket, park::PlacedPerson { .world = glm::translate(glm::mat4(1.0f), glm::vec3(50.0f)) });
    REQUIRE(hand.has_value());
    // In the person's own space, wherever they stand.
    CHECK(hand->x == doctest::Approx(0.11f));
    CHECK(hand->y == doctest::Approx(1.01f));
    CHECK(hand->z == doctest::Approx(0.21f));
}

TEST_CASE("a walk's pace is how fast the foot on the ground slides back beneath the walker")
{
    // Two points: a foot on the ground sliding back 0.1 a key, and a knee above it swinging ahead.
    park::PieceModel legs;
    legs.mesh.points = 2;
    legs.mesh.normals = 1;
    ovl::FrameAnimation walk;
    walk.scale = { 0.01f, 0.01f, 0.01f };
    walk.times = { 0.25f, 0.5f, 0.75f, 1.0f };
    for (std::uint32_t k = 0; k < 4; ++k)
    {
        const std::array<std::array<std::uint32_t, 3>, 2> steps { { { 10, 0, 60 - 10 * k }, { 10, 50, 60 + 10 * k } } };
        for (const auto& [x, y, z] : steps)
        {
            const std::uint32_t word = x << 21 | y << 11 | z;
            for (int b = 0; b < 4; ++b)
                walk.keys.push_back(static_cast<std::byte>(word >> (8 * b)));
        }
        walk.keys.resize((k + 1) * legs.mesh.key_size(), std::byte { 127 });
    }
    legs.mesh.animations.push_back(walk);
    // Three of the four steps slide the foot back 0.1 in 0.25 s; the last brings it forward again.
    const auto pace = park::walk_pace(legs, park::motion::kWalk);
    REQUIRE(pace.has_value());
    CHECK(*pace == doctest::Approx(0.4f));

    legs.mesh.animations.clear();
    CHECK_FALSE(park::walk_pace(legs, park::motion::kWalk).has_value());
}
