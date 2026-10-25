// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/animal_mesh.hpp"

#include <doctest/doctest.h>

using namespace rct3::park;
using rct3::ovl::AnimalMotion;

namespace
{

    constexpr float kTurn = 1.5707963f; // a quarter turn

    /// A root at the origin and a child one unit up, a stand that turns the root a quarter about z over two
    /// frames, and a walk that lifts the child.
    AnimalBody two_bones()
    {
        AnimalBody body;
        body.parents = { -1, 0 };
        body.rest_positions = { glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f) };
        body.rest_rotations = { glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0) };
        body.motions.resize(static_cast<std::size_t>(AnimalMotion::TurnRight90) + 1);
        AnimalClip stand { .seconds = 2.0f / kAnimalFramesPerSecond, .frames = 3, .turning = { 0 } };
        stand.rotations = { glm::quat(1, 0, 0, 0), glm::angleAxis(kTurn / 2, glm::vec3(0, 0, 1)), glm::angleAxis(kTurn, glm::vec3(0, 0, 1)) };
        body.motions[static_cast<std::size_t>(AnimalMotion::Stand)] = stand;
        AnimalClip walk { .seconds = 1.0f / kAnimalFramesPerSecond, .frames = 2, .moving = { 1 } };
        walk.positions = { glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 3.0f, 0.0f) };
        body.motions[static_cast<std::size_t>(AnimalMotion::Walk)] = walk;
        AnimalPiece piece { .picture = "skin" };
        piece.vertices = { { .position = { 0, 2, 0 }, .normal = { 1, 0, 0 }, .bones = { 1, 0, 0, 0 }, .weights = { 255, 0, 0, 0 } }, { .position = { 1, 0, 0 }, .bones = { 0, 0, 0, 0 }, .weights = { 255, 0, 0, 0 } }, { .position = { 0, 0, 1 } } };
        piece.indices = { 0, 1, 2 };
        body.levels = { { piece }, { } };
        return body;
    }

    void check_near(const glm::vec3& a, const glm::vec3& b)
    {
        CHECK(a.x == doctest::Approx(b.x).epsilon(1e-4));
        CHECK(a.y == doctest::Approx(b.y).epsilon(1e-4));
        CHECK(a.z == doctest::Approx(b.z).epsilon(1e-4));
    }

} // namespace

TEST_CASE("a motion the animal lacks leaves every bone at rest")
{
    const AnimalBody body = two_bones();
    for (const glm::mat4& move : animal_pose(body, AnimalMotion::Scratch, 1.0f))
        check_near(move * glm::vec4(1, 2, 3, 1), { 1, 2, 3 });
}

TEST_CASE("a turned bone carries its children and their vertices")
{
    const AnimalBody body = two_bones();
    const auto pose = animal_pose(body, AnimalMotion::Stand, 2.0f / kAnimalFramesPerSecond - 1e-6f);
    // At the end of the stand the root has turned a quarter about z: up becomes -x.
    check_near(pose[1] * glm::vec4(0, 2, 0, 1), { -2, 0, 0 });
    check_near(pose[0] * glm::vec4(1, 0, 0, 1), { 0, 1, 0 });
}

TEST_CASE("frames between keys blend and the motion repeats")
{
    const AnimalBody body = two_bones();
    const float frame = 1.0f / kAnimalFramesPerSecond;
    check_near(animal_pose(body, AnimalMotion::Walk, frame / 2)[1] * glm::vec4(0, 1, 0, 1), { 0, 2, 0 });
    check_near(animal_pose(body, AnimalMotion::Walk, frame + frame / 2)[1] * glm::vec4(0, 1, 0, 1), { 0, 2, 0 });
    check_near(animal_pose(body, AnimalMotion::Stand, frame)[1] * glm::vec4(0, 1, 0, 1), glm::vec3(glm::angleAxis(kTurn / 2, glm::vec3(0, 0, 1)) * glm::vec3(0, 1, 0)));
}

TEST_CASE("motion lengths come from the clips")
{
    const AnimalBody body = two_bones();
    CHECK(motion_seconds(body, AnimalMotion::Walk) == doctest::Approx(1.0f / kAnimalFramesPerSecond));
    CHECK(motion_seconds(body, AnimalMotion::Run) == 0.0f);
}

TEST_CASE("an animal's mesh is skinned, placed and batched by picture")
{
    const AnimalBody body = two_bones();
    AnimalPiece glass { .picture = "glass", .blend = PartBlend::Blended };
    glass.vertices = { { .position = { 0, 0, 0 } }, { .position = { 1, 0, 0 } }, { .position = { 0, 1, 0 } } };
    glass.indices = { 0, 1, 2 };
    AnimalBody with_glass = body;
    with_glass.levels[0].insert(with_glass.levels[0].begin(), glass);

    const PlacedAnimal animal { .body = &with_glass, .level = 5, .motion = AnimalMotion::Walk, .time = 0.5f / kAnimalFramesPerSecond, .world = glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 0)) };
    AnimalRuns runs;
    const ObjectMesh mesh = animal_mesh({ &animal, 1 }, runs);
    CHECK(mesh.placed == 1);
    REQUIRE(mesh.batches.size() == 2);
    // Solid first, then see-through, whatever order the pieces came in; the empty coarse level falls back.
    CHECK(mesh.batches[0].picture == "skin");
    CHECK(mesh.batches[1].blend == PartBlend::Blended);
    REQUIRE(mesh.vertices.size() == 6);
    // Halfway through the walk the child has risen one unit, and the animal stands ten units along x.
    check_near(mesh.vertices[0].position, { 10, 3, 0 });
    check_near(mesh.vertices[0].normal, { 1, 0, 0 });
    // A vertex with no weights stays where the model has it.
    check_near(mesh.vertices[2].position, { 10, 0, 1 });
    CHECK(mesh.indices.size() == 6);
    CHECK(mesh.indices[3] == 3);
}
