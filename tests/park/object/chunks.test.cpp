// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object/chunks.hpp"

#include <cstdint>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park::object;

namespace
{
    save::model::PlacedObject at(std::int32_t col, std::int32_t row)
    {
        save::model::PlacedObject o;
        o.col = col;
        o.row = row;
        return o;
    }

    LevelPlan plan(ObjectLevels steady)
    {
        const std::size_t n = steady.size();
        return { .steady = std::move(steady), .finer = ObjectLevels(n, kNotDrawn), .coarser = ObjectLevels(n, kNotDrawn), .bands = std::vector<glm::vec2>(n) };
    }
} // namespace

TEST_CASE("objects fall in the chunk of their tile, those off the map in the nearest along its edge")
{
    const ChunkGrid grid = ChunkGrid::of(70, 40, 32);
    CHECK(grid.across == 3);
    CHECK(grid.down == 2);
    CHECK(grid.count() == 6);
    CHECK(grid.at(0, 0) == 0);
    CHECK(grid.at(31, 31) == 0);
    CHECK(grid.at(32, 0) == 1);
    CHECK(grid.at(69, 39) == 5);
    CHECK(grid.at(-3, 50) == 3);
    CHECK(grid.at(500, -1) == 2);

    const std::vector objects { at(40, 5), at(1, 1), at(40, 35), at(33, 2) };
    const auto members = chunk_members(objects, grid);
    REQUIRE(members.size() == 6);
    CHECK(members[0] == std::vector<std::uint32_t> { 1 });
    CHECK(members[1] == std::vector<std::uint32_t> { 0, 3 });
    CHECK(members[4] == std::vector<std::uint32_t> { 2 });
    CHECK(members[2].empty());
}

TEST_CASE("only the chunks holding an object planned differently have changed, all when the objects did")
{
    const std::vector<std::vector<std::uint32_t>> members { { 0, 2 }, { 1 }, { } };
    const LevelPlan shown = plan({ 0, 1, 2 });
    CHECK(changed_chunks(shown, shown, members) == std::vector { false, false, false });
    CHECK(changed_chunks(plan({ 0, 2, 2 }), shown, members) == std::vector { false, true, false });

    LevelPlan crossing = shown;
    crossing.bands[2] = { 10.0f, 12.0f };
    CHECK(changed_chunks(crossing, shown, members) == std::vector { true, false, false });

    CHECK(changed_chunks(plan({ 0, 1, 2, 0 }), shown, members) == std::vector { true, true, true });
    CHECK(changed_chunks(shown, { }, members) == std::vector { true, true, true });
}

TEST_CASE("a chunk's box holds its shapes and its billboards turned any way")
{
    ShapeBox box;
    CHECK(box.empty);
    ObjectMesh mesh;
    mesh.vertices.push_back({ .position = { 1, 2, 3 } });
    mesh.vertices.push_back({ .position = { -1, 5, 4 } });
    box.take(mesh);
    CHECK_FALSE(box.empty);
    CHECK(box.low == glm::vec3(-1, 2, 3));
    CHECK(box.high == glm::vec3(1, 5, 4));

    Billboards boards;
    boards.stands.push_back({ .base = { 10, 0, 10 }, .half_width = 2, .height = 6 });
    box.take(boards);
    CHECK(box.low == glm::vec3(-1, 0, 3));
    CHECK(box.high == glm::vec3(12, 6, 12));
}
