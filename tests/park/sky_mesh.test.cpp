// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sky_mesh.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    SkyShape triangle(std::string texture, float size)
    {
        SkyShape s;
        s.texture = std::move(texture);
        s.vertices = { { { size, 0, 0 }, { }, { 0.5f, 0.0f } }, { { 0, size, 0 }, { }, { 0.5f, 1.0f } }, { { 0, 0, size }, { }, { 0.25f, 0.5f } } };
        s.indices = { 0, 1, 2 };
        return s;
    }

    SkyModels sky()
    {
        SkyModels m;
        m.dome = triangle("dome", 10);
        m.stars = triangle("stars", 8);
        m.milky_way = triangle("band", 8);
        m.clouds = triangle("clouds", 20);
        return m;
    }
} // namespace

TEST_CASE("the sky is centred on the eye, its farthest point at the radius")
{
    const ObjectMesh m = sky_mesh(sky(), { .eye = { 100, 5, -40 }, .radius = 1000 });
    // Clouds reach farthest: scaled to the radius, the dome half as far.
    REQUIRE(m.batches.size() == 3);
    CHECK(m.batches[0].texture == "dome");
    CHECK(m.batches[0].blend == PartBlend::Opaque);
    const GroundVertex& dome = m.vertices[m.indices[m.batches[0].first]];
    CHECK(dome.position.x == doctest::Approx(600));
    CHECK(dome.position.y == doctest::Approx(5));
    CHECK(dome.position.z == doctest::Approx(-40));
    const GroundVertex& cloud = m.vertices[m.indices[m.batches[1].first + 1]];
    CHECK(cloud.position.y == doctest::Approx(1005));

    // Moving the eye moves the sky with it.
    const ObjectMesh moved = sky_mesh(sky(), { .eye = { 0, 0, 0 }, .radius = 1000 });
    CHECK(moved.vertices[0].position.x == doctest::Approx(500));
}

TEST_CASE("the dome reads one column of its picture, for the time of day")
{
    const ObjectMesh m = sky_mesh(sky(), { .radius = 1, .day = 0.8f });
    for (std::uint32_t i = 0; i < m.batches[0].count; ++i)
        CHECK(m.vertices[m.indices[m.batches[0].first + i]].uv.x == doctest::Approx(0.8f));
    CHECK(m.vertices[m.indices[1]].uv.y == doctest::Approx(1));
}

TEST_CASE("a fading look is the dome again in its column, drawn over the other")
{
    const ObjectMesh m = sky_mesh(sky(), { .radius = 1, .day = 0.1f, .faded_day = 0.7f, .fade = 0.25f });
    REQUIRE(m.batches.size() == 4);
    CHECK(m.batches[1].texture == "dome");
    CHECK(m.batches[1].blend == PartBlend::Blended);
    const GroundVertex& v = m.vertices[m.indices[m.batches[1].first]];
    CHECK(v.uv.x == doctest::Approx(0.7f));
    CHECK(v.colour.a == doctest::Approx(0.25f));
    CHECK(m.vertices[m.indices[m.batches[0].first]].uv.x == doctest::Approx(0.1f));
}

TEST_CASE("stars show only at night, and clouds drift in two layers")
{
    const ObjectMesh day = sky_mesh(sky(), { .radius = 1 });
    CHECK(day.batches.size() == 3);
    CHECK(day.batches[1].texture == "clouds");
    CHECK(day.batches[2].texture == "clouds");
    CHECK(day.batches[1].blend == PartBlend::Blended);

    const ObjectMesh night = sky_mesh(sky(), { .radius = 1, .night = 1 });
    REQUIRE(night.batches.size() == 5);
    CHECK(night.batches[1].texture == "stars");
    CHECK(night.batches[2].texture == "band");
    // Clouds are darker at night.
    CHECK(night.vertices[night.indices[night.batches[3].first]].colour.r < day.vertices[day.indices[day.batches[1].first]].colour.r);

    const ObjectMesh later = sky_mesh(sky(), { .radius = 1, .time = 10 });
    const glm::vec2 before = day.vertices[day.indices[day.batches[1].first]].uv;
    const glm::vec2 after = later.vertices[later.indices[later.batches[1].first]].uv;
    CHECK(after.x == doctest::Approx(before.x + 10 * kCloudDrift));
    // The second layer repeats twice as often.
    CHECK(day.vertices[day.indices[day.batches[2].first + 1]].uv.y == doctest::Approx(2));
}

TEST_CASE("an overcast sky is darker, its clouds thicker, and it hides the stars")
{
    const ObjectMesh clear = sky_mesh(sky(), { .radius = 1 });
    const ObjectMesh grey = sky_mesh(sky(), { .radius = 1, .tint = glm::vec3(0.5f), .overcast = 1 });
    CHECK(grey.vertices[grey.indices[grey.batches[0].first]].colour.r == doctest::Approx(0.5f));
    const auto cloud = [](const ObjectMesh& m) { return m.vertices[m.indices[m.batches[1].first]].colour; };
    CHECK(cloud(grey).r < cloud(clear).r);
    CHECK(cloud(grey).a > cloud(clear).a);
    // At night the stars are there but do not show through.
    const ObjectMesh night = sky_mesh(sky(), { .radius = 1, .night = 1, .overcast = 1 });
    CHECK(night.batches.size() == 3);
}

TEST_CASE("an empty sky draws nothing")
{
    CHECK(sky_mesh({ }, { .radius = 1 }).batches.empty());
}
