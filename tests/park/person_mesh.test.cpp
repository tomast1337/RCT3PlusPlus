// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/person_mesh.hpp"

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
    const park::ObjectMesh mesh = park::person_mesh(library, { });
    CHECK(mesh.vertices.empty());
    CHECK(mesh.batches.empty());
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
