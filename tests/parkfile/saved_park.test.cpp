// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/saved_park.hpp"

#include <doctest/doctest.h>

using namespace rct3;

namespace
{
    parkfile::SavedPark small_park()
    {
        parkfile::SavedPark park;
        park.base = { std::byte { 1 }, std::byte { 2 }, std::byte { 3 } };
        park.summary.park_name = "Example Park";
        park.summary.preview = { 9, 8, 7, 6 };
        park.grid.width = 2;
        park.grid.height = 1;
        park.grid.cells.resize(2);
        park.paths.emplace_back().col = 5;
        save::PlacedObject& o = park.objects.emplace_back();
        o.id = 42;
        o.symbol = "bench";
        o.base_height = 3;
        o.height_adjust = 0.5f;
        o.colours = { 1, 2, 3 };
        park.rides.stalls.emplace_back();
        park.progress.cash = 123456;
        park.progress.date.year = 2;
        park.progress.forecast = save::WeatherType::Unknown;
        park.progress.day_time = 0.25f;
        park.camera.target = { 1, 2, 3 };
        return park;
    }
} // namespace

TEST_CASE("a saved park reads back as written")
{
    const auto bytes = parkfile::write_saved_park(small_park());
    REQUIRE(parkfile::is_park_file(bytes));
    const auto read = parkfile::read_saved_park(bytes);
    REQUIRE_MESSAGE(read, (read ? "" : read.error()));
    CHECK(read->base.size() == 3);
    CHECK(read->summary.park_name == "Example Park");
    CHECK(read->summary.preview == std::vector<std::uint8_t> { 9, 8, 7, 6 });
    CHECK(read->grid.width == 2);
    CHECK(read->grid.cells.size() == 2);
    CHECK(read->paths.at(0).col == 5);
    CHECK(read->objects.at(0).id == 42);
    CHECK(read->objects.at(0).symbol == "bench");
    CHECK(read->objects.at(0).base_height == 3);
    CHECK(read->objects.at(0).height_adjust == 0.5f);
    CHECK(read->objects.at(0).colours == std::array<std::int32_t, 3> { 1, 2, 3 });
    CHECK(read->rides.stalls.size() == 1);
    CHECK(read->progress.cash == 123456);
    CHECK(read->progress.date.year == 2);
    CHECK(read->progress.day_time == 0.25f);
    CHECK(read->camera.target == glm::vec3 { 1, 2, 3 });
    CHECK(parkfile::write_saved_park(*read) == bytes);
}

TEST_CASE("a cut or changed saved park is refused")
{
    auto bytes = parkfile::write_saved_park(small_park());
    CHECK_FALSE(parkfile::read_saved_park(std::span(bytes).first(bytes.size() - 1)));
    // The first chunk's layout hash, just after its header.
    bytes[16 + 16] ^= std::byte { 1 };
    CHECK_FALSE(parkfile::read_saved_park(bytes));
    CHECK_FALSE(parkfile::is_park_file(std::vector<std::byte>(4)));
}

TEST_CASE("a save keeps the park's objects and those the player built, not those loading makes")
{
    std::vector<save::PlacedObject> objects(5);
    for (std::size_t i = 0; i < objects.size(); ++i)
        objects[i].id = 10 + i;
    // 10 and 11 are the park's; 12 a support made on loading; 13 a path's deck; 14 a track piece.
    std::vector<save::PathTile> paths(1);
    paths[0].elevation.emplace().scenery_ref = 13;
    save::RideModel rides;
    rides.rides.emplace_back().track.emplace().pieces.emplace_back().scenery = 14;
    const auto kept = parkfile::kept_objects(objects, 2, paths, rides);
    REQUIRE(kept.size() == 4);
    CHECK(kept[0].id == 10);
    CHECK(kept[1].id == 11);
    CHECK(kept[2].id == 13);
    CHECK(kept[3].id == 14);
}
