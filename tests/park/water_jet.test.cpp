// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_jet.hpp"

#include <cmath>
#include <doctest/doctest.h>

using namespace rct3::park;

namespace
{

    float flat(glm::vec2)
    {
        return 0;
    }

} // namespace

TEST_CASE("a level jet lands as far as a thrown stone would")
{
    WaterJet jet(3, 0.1f, 0);
    jet.aim({ 0, 5, 0 }, { 6, 0, 0 });
    for (int i = 0; i < 120; ++i)
        jet.advance(1 / 60.0f);
    const auto land = jet.landing(flat);
    REQUIRE(land.has_value());
    // Falling 5 m takes sqrt(2 * 5 / g) seconds.
    CHECK(land->x == doctest::Approx(6 * std::sqrt(10 / kJetGravity)).epsilon(0.05));
    CHECK(land->y == doctest::Approx(0).epsilon(0.01));
    CHECK(land->z == doctest::Approx(0));
}

TEST_CASE("a jet's newest drop stays on the nozzle as it swings")
{
    WaterJet jet(1, 0.1f, 0.5f);
    jet.aim({ 0, 1, 0 }, { 0, 0, 5 });
    jet.advance(0.5f);
    jet.aim({ 1, 1, 0 }, { 5, 0, 0 });
    CHECK(jet.drops().front().position == glm::vec3(1, 1, 0));
    CHECK(jet.drops().back().velocity.z == doctest::Approx(5));
    CHECK(jet.radius(jet.drops().back()) > 0.1f);
}

TEST_CASE("a stopped jet runs out once its last drop has lived")
{
    WaterJet jet(0.5f, 0.1f, 0);
    jet.aim({ 0, 1, 0 }, { 0, 0, 5 });
    jet.advance(0.3f);
    jet.stop();
    CHECK_FALSE(jet.finished());
    jet.advance(0.6f);
    CHECK(jet.finished());
}

TEST_CASE("a jet's tube is rings round its drops, more see-through face on")
{
    WaterJet jet(1, 0.1f, 0);
    jet.aim({ 0, 1, 0 }, { 0, 0, 5 });
    jet.advance(0.2f);
    const std::array<const WaterJet*, 1> jets { &jet };
    ObjectMesh mesh;
    jet_mesh(jets, { 10, 1, 0 }, 0, mesh);
    REQUIRE(mesh.batches.size() == 1);
    CHECK(mesh.batches[0].texture == kJetPicture);
    CHECK(mesh.vertices.size() == jet.drops().size() * 9);
    CHECK(mesh.indices.size() == (jet.drops().size() - 1) * 8 * 6);
    // At the nozzle the ring's side facing the eye is clearer than its top, seen edge on.
    float facing = 1, edge = 0;
    for (std::size_t k = 0; k < 9; ++k)
    {
        const GroundVertex& v = mesh.vertices[k];
        if (v.normal.x > 0.9f)
            facing = v.colour.a;
        if (v.normal.y > 0.9f)
            edge = v.colour.a;
    }
    CHECK(facing < edge);
}

TEST_CASE("a throw lands where it is aimed, up or down hill")
{
    for (const float rise : { 0.0f, 0.5f, -1.0f })
    {
        const glm::vec3 from { 1, 2, 3 };
        const glm::vec3 to { 5, 2 + rise, 0 };
        const auto t = throw_to(from, to, 0.9f);
        REQUIRE(t.has_value());
        const glm::vec3 at = from + t->velocity * t->seconds + glm::vec3(0, -kJetGravity / 2, 0) * t->seconds * t->seconds;
        CHECK(glm::distance(at, to) < 1e-3f);
        CHECK(std::atan2(t->velocity.y, std::hypot(t->velocity.x, t->velocity.z)) == doctest::Approx(0.9f));
    }
    CHECK_FALSE(throw_to({ 0, 0, 0 }, { 1, 5, 0 }, 0.9f).has_value());
}
