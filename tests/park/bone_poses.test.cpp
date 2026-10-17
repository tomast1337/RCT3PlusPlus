// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/bone_poses.hpp"

#include <doctest/doctest.h>
#include <numbers>

using namespace rct3::park;

namespace
{
    bool near(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::length(a - b) < 1e-4f;
    }
} // namespace

TEST_CASE("a rotation key turns about its direction by its length")
{
    const glm::mat4 turn = key_turn({ 0, 0, std::numbers::pi_v<float> / 2 });
    CHECK(near(glm::vec3(turn * glm::vec4(1, 0, 0, 1)), { 0, 1, 0 }));
    CHECK(near(glm::vec3(key_turn({ 0, 0, 0 }) * glm::vec4(1, 2, 3, 1)), { 1, 2, 3 }));
}

TEST_CASE("a bone carries the bones hung from it, between its keys")
{
    // Bone 0 hangs a metre along x from bone 1, the root; the root turns a quarter turn about y in 2 s.
    BoneRig rig;
    rig.parents = { 1, -1 };
    rig.rest_local = { glm::translate(glm::mat4(1.0f), { 1, 0, 0 }), glm::mat4(1.0f) };
    rig.rest_world = rig.rest_local;
    BoneMotion turn;
    turn.seconds = 2;
    turn.positions.resize(2);
    turn.rotations.resize(2);
    turn.rotations[1] = { { 0, 2 }, { { 0, 0, 0 }, { 0, std::numbers::pi_v<float> / 2, 0 } } };
    rig.motions = { turn };

    const auto standing = bone_moves(rig, 0, 0);
    CHECK(near(glm::vec3(standing[0] * glm::vec4(1, 0, 0, 1)), { 1, 0, 0 }));
    // Halfway, an eighth of a turn: the hung bone has swung from +x towards -z, and its up stays up.
    const float r = std::numbers::sqrt2_v<float> / 2;
    const auto halfway = bone_moves(rig, 0, 1);
    CHECK(near(glm::vec3(halfway[0] * glm::vec4(1, 0, 0, 1)), { r, 0, -r }));
    CHECK(near(glm::vec3(halfway[0] * glm::vec4(1, 1, 0, 1)), { r, 1, -r }));
    // Past its last key a motion holds it; one the rig lacks leaves it standing.
    CHECK(near(glm::vec3(bone_moves(rig, 0, 5)[0] * glm::vec4(1, 0, 0, 1)), { 0, 0, -1 }));
    CHECK(near(glm::vec3(bone_moves(rig, 3, 1)[0] * glm::vec4(1, 0, 0, 1)), { 1, 0, 0 }));
}

TEST_CASE("a flat ride starts, runs round and stops within its ride, and stands between rides")
{
    const float seconds[] = { 5, 2, 3, 1 }; // Standing, starting, running, stopping
    CHECK(flat_ride_motion(seconds, 10, 1, true, 0) == std::pair<std::size_t, float> { kRideStarting, 1.0f });
    CHECK(flat_ride_motion(seconds, 10, 7, true, 0) == std::pair<std::size_t, float> { kRideRunning, 2.0f });
    CHECK(flat_ride_motion(seconds, 10, 9.5f, true, 0) == std::pair<std::size_t, float> { kRideStopping, 0.5f });
    CHECK(flat_ride_motion(seconds, 10, 0, false, 7) == std::pair<std::size_t, float> { kRideStanding, 2.0f });
}
