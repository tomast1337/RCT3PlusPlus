// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/waterfall_mesh.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // A fall 8 wide pouring along +X off an edge at height 20, landing 10 out on a lake at height 5.
    save::Waterfall fall()
    {
        return {
            .lip_left = { 0.0f, 20.0f, -4.0f },
            .lip_right = { 0.0f, 20.0f, 4.0f },
            .top = { -6.0f, 20.0f, 0.0f },
            .bottom = { 10.0f, 5.0f, 0.0f },
            .flow = { 1.0f, 0.0f, 0.0f },
        };
    }

} // namespace

TEST_CASE("waterfall_point: leaves the edge level and lands where the park says")
{
    const save::Waterfall f = fall();
    const glm::vec3 start = waterfall_point(f, 0.0f);
    CHECK(start.x == doctest::Approx(0.0f));
    CHECK(start.y == doctest::Approx(20.0f));
    const glm::vec3 end = waterfall_point(f, 1.0f);
    CHECK(end.x == doctest::Approx(10.0f));
    CHECK(end.y == doctest::Approx(5.0f));
    // Falls ever faster: the second half drops three times as far as the first.
    const glm::vec3 middle = waterfall_point(f, 0.5f);
    CHECK(start.y - middle.y == doctest::Approx((middle.y - end.y) / 3.0f));
}

TEST_CASE("waterfall_point: a fall whose direction is missing runs toward where it lands")
{
    save::Waterfall f = fall();
    f.flow = { };
    f.bottom = { 0.0f, 5.0f, -10.0f };
    f.lip_left = { -4.0f, 20.0f, 0.0f };
    f.lip_right = { 4.0f, 20.0f, 0.0f };
    CHECK(waterfall_point(f, 1.0f).z == doctest::Approx(-10.0f));
}

TEST_CASE("waterfall_mesh: one see-through batch per picture, the sheet as wide as the edge")
{
    const std::vector<save::Waterfall> falls { fall(), fall() };
    ObjectMesh mesh;
    waterfall_mesh(falls, 0.0f, mesh);
    REQUIRE(mesh.batches.size() == kWaterfallPictures.size());
    for (std::size_t i = 0; i < mesh.batches.size(); ++i)
    {
        CHECK(mesh.batches[i].texture == kWaterfallPictures[i].key);
        CHECK(mesh.batches[i].blend == PartBlend::Blended);
        CHECK(mesh.batches[i].count % 6 == 0);
    }
    CHECK(mesh.batches[0].count == mesh.batches[1].count);
    CHECK(mesh.batches[2].count == 2 * 9 * 6); // A foam patch of three by three squares each
    float low_z = 0, high_z = 0;
    for (std::uint32_t i = mesh.batches[0].first; i < mesh.batches[0].first + mesh.batches[0].count; ++i)
    {
        low_z = std::min(low_z, mesh.vertices[mesh.indices[i]].position.z);
        high_z = std::max(high_z, mesh.vertices[mesh.indices[i]].position.z);
    }
    CHECK(low_z == doctest::Approx(-4.0f));
    CHECK(high_z == doctest::Approx(4.0f));
}

TEST_CASE("waterfall_mesh: the sheet's picture runs down it as time passes")
{
    const std::vector<save::Waterfall> falls { fall() };
    ObjectMesh before;
    ObjectMesh after;
    waterfall_mesh(falls, 0.0f, before);
    waterfall_mesh(falls, 1.0f, after);
    REQUIRE(before.vertices.size() == after.vertices.size());
    const auto at = before.indices[before.batches[0].first];
    CHECK(after.vertices[at].uv.y < before.vertices[at].uv.y);
    CHECK(after.vertices[at].position.x == before.vertices[at].position.x);
}

TEST_CASE("waterfall_mesh: nothing for no falls, and the mesh is emptied")
{
    const std::vector<save::Waterfall> falls { fall() };
    ObjectMesh mesh;
    waterfall_mesh(falls, 0.0f, mesh);
    waterfall_mesh({ }, 0.0f, mesh);
    CHECK(mesh.vertices.empty());
    CHECK(mesh.indices.empty());
    CHECK(mesh.batches.empty());
}

TEST_CASE("waterfall_mesh: foam fades out at its edges and is whole in its middle")
{
    const std::vector<save::Waterfall> falls { fall() };
    ObjectMesh mesh;
    waterfall_mesh(falls, 0.0f, mesh);
    const ObjectBatch& foam = mesh.batches[2];
    int clear = 0;
    int whole = 0;
    for (std::uint32_t i = foam.first; i < foam.first + foam.count; ++i)
    {
        const float alpha = mesh.vertices[mesh.indices[i]].colour.a;
        clear += alpha == 0.0f;
        whole += alpha == 1.0f;
    }
    CHECK(clear > 0);
    CHECK(whole > 0);
    CHECK(clear + whole == static_cast<int>(foam.count));
}
