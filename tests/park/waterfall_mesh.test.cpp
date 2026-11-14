// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/waterfall_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <utility>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // A fall 8 wide pouring along +X off an edge at height 20, landing 10 out on a lake at height 5.
    save::model::Waterfall fall()
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
    const save::model::Waterfall f = fall();
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
    save::model::Waterfall f = fall();
    f.flow = { };
    f.bottom = { 0.0f, 5.0f, -10.0f };
    f.lip_left = { -4.0f, 20.0f, 0.0f };
    f.lip_right = { 4.0f, 20.0f, 0.0f };
    CHECK(waterfall_point(f, 1.0f).z == doctest::Approx(-10.0f));
}

namespace
{

    std::size_t part(const WaterfallMesh& mesh, FallPart p)
    {
        return mesh.parts[static_cast<std::size_t>(p)].count;
    }

    /// The mist batch's puffs and droplets, by the sign of their normal's last value.
    std::pair<int, int> puffs_and_droplets(const WaterfallMesh& mesh)
    {
        const FallBatch mist = mesh.parts[static_cast<std::size_t>(FallPart::Mist)];
        std::pair<int, int> counted { };
        for (std::uint32_t i = mist.first; i < mist.first + mist.count; i += 6)
            ++(mesh.vertices[mesh.indices[i]].normal.z > 0 ? counted.first : counted.second);
        return counted;
    }

} // namespace

TEST_CASE("fall_seconds: as long as a stone takes to fall as far")
{
    CHECK(fall_seconds({ 0.0f }).value == 0.0f);
    CHECK(fall_seconds({ 15.0f }).value == doctest::Approx(std::sqrt(2.0f * 15.0f / 9.81f)));
    CHECK(fall_seconds({ 60.0f }).value == doctest::Approx(2.0f * fall_seconds({ 15.0f }).value));
}

TEST_CASE("waterfall_mesh: foam and sheet at every level, mist from Medium, more of it from High, droplets at Ultra")
{
    save::model::Waterfall tall = fall();
    tall.bottom.y = -20.0f; // A 40 unit drop
    tall.lip_left.x = tall.lip_right.x = 50.0f;
    tall.bottom.x = 60.0f;
    const std::vector<save::model::Waterfall> falls { fall(), tall };
    const std::vector<FallLakes> lakes(2);
    WaterfallMesh mesh;
    std::array<std::pair<int, int>, 4> mist { };
    for (const WaterfallDetail detail : { WaterfallDetail::Low, WaterfallDetail::Medium, WaterfallDetail::High, WaterfallDetail::Ultra })
    {
        CAPTURE(static_cast<int>(detail));
        waterfall_mesh(falls, lakes, detail, mesh);
        CHECK(part(mesh, FallPart::Foam) == 2 * 6);
        CHECK(part(mesh, FallPart::Sheet) == 2 * 13 * 6); // Twelve rows down and one behind the edge each
        mist[static_cast<std::size_t>(detail)] = puffs_and_droplets(mesh);
    }
    CHECK(mist[0] == std::pair { 0, 0 });
    CHECK(mist[1].first > 0);
    CHECK(mist[1].second == 0);
    CHECK(mist[2].first == 2 * mist[1].first);
    CHECK(mist[2].second == 0);
    CHECK(mist[3].first == mist[2].first);
    CHECK(mist[3].second > 0);
}

TEST_CASE("waterfall_mesh: the sheet's last row carries the time the whole fall takes")
{
    const std::vector<save::model::Waterfall> falls { fall() };
    WaterfallMesh mesh;
    waterfall_mesh(falls, std::vector<FallLakes>(1), WaterfallDetail::High, mesh);
    const FallBatch sheet = mesh.parts[static_cast<std::size_t>(FallPart::Sheet)];
    const GroundVertex& last = mesh.vertices[mesh.indices[sheet.first + sheet.count - 1]];
    CHECK(last.uv.y == doctest::Approx(fall_seconds({ 15.0f }).value));
    CHECK(last.position.y == doctest::Approx(5.0f));
}

TEST_CASE("waterfall_mesh: no mist under a ten unit drop, at any level")
{
    save::model::Waterfall low = fall();
    low.bottom.y = 12.0f; // An 8 unit drop
    WaterfallMesh mesh;
    waterfall_mesh(std::span(&low, 1), { }, WaterfallDetail::High, mesh);
    CHECK(part(mesh, FallPart::Mist) == 0);
    waterfall_mesh(std::span(&low, 1), { }, WaterfallDetail::Ultra, mesh);
    CHECK(puffs_and_droplets(mesh).first == 0);
    CHECK(puffs_and_droplets(mesh).second > 0); // Droplets splash up off any fall
}

TEST_CASE("waterfall_mesh: a second fall over the same edge keeps only what the first leaves")
{
    const save::model::Waterfall first = fall();
    save::model::Waterfall wider = fall();
    wider.lip_right.z = 10.0f; // From -4 to 10: 6 units past the first
    WaterfallMesh mesh;
    const auto sheet_ends = [&] {
        const FallBatch sheet = mesh.parts[static_cast<std::size_t>(FallPart::Sheet)];
        float low = 1e9f;
        float high = -1e9f;
        for (std::uint32_t i = sheet.first; i < sheet.first + sheet.count; ++i)
        {
            low = std::min(low, mesh.vertices[mesh.indices[i]].position.z);
            high = std::max(high, mesh.vertices[mesh.indices[i]].position.z);
        }
        return std::pair { low, high };
    };
    const std::vector<save::model::Waterfall> same { first, first };
    waterfall_mesh(same, { }, WaterfallDetail::Low, mesh);
    CHECK(part(mesh, FallPart::Sheet) == 13 * 6);
    const std::vector<save::model::Waterfall> both { first, wider };
    waterfall_mesh(both, { }, WaterfallDetail::Low, mesh);
    CHECK(part(mesh, FallPart::Sheet) == 2 * 13 * 6);
    CHECK(sheet_ends().first == doctest::Approx(-4.0f));
    CHECK(sheet_ends().second == doctest::Approx(10.0f));
    // The second's piece starts where the first ends: no stretch of edge is poured over twice.
    const FallBatch sheet = mesh.parts[static_cast<std::size_t>(FallPart::Sheet)];
    const float second_start = mesh.vertices[mesh.indices[sheet.first + 13 * 6]].position.z;
    CHECK(second_start == doctest::Approx(4.0f));
    // A fall over another edge, or to another lake, is left whole.
    save::model::Waterfall elsewhere = wider;
    elsewhere.bottom.y = 0.0f;
    const std::vector<save::model::Waterfall> apart { first, elsewhere };
    waterfall_mesh(apart, { }, WaterfallDetail::Low, mesh);
    CHECK(sheet_ends().second == doctest::Approx(10.0f));
    CHECK(mesh.vertices[mesh.indices[mesh.parts[static_cast<std::size_t>(FallPart::Sheet)].first + 13 * 6]].position.z == doctest::Approx(-4.0f));
}

TEST_CASE("waterfall_mesh: nothing for no falls, and the mesh is emptied")
{
    const std::vector<save::model::Waterfall> falls { fall() };
    WaterfallMesh mesh;
    waterfall_mesh(falls, { }, WaterfallDetail::Ultra, mesh);
    waterfall_mesh({ }, { }, WaterfallDetail::Ultra, mesh);
    CHECK(mesh.vertices.empty());
    CHECK(mesh.indices.empty());
    for (const FallBatch& b : mesh.parts)
        CHECK(b.count == 0);
}
