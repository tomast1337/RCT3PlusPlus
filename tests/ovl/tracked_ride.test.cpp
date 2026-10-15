// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/tracked_ride.hpp"

#include "record_file.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::test;
using rct3::ovl::read_tracked_rides;

TEST_CASE("version 1 tracked rides start with the attraction fields")
{
    Pool pool;
    pool.add("Coaster:trr");
    pool.add("CoasterName:txt");
    pool.add("CoasterDescription:txt");
    pool.add("CoasterIcon:gsi");
    pool.add("CoasterSpline:spl");
    std::vector<std::byte> bytes;
    put(bytes, { 3, 0, 0, 0, 10000, static_cast<std::uint32_t>(-7500), 0, 0, 2500, 0 });
    bytes.resize(0x28 + 8, std::byte { 0 });
    const auto overlay = make_record_overlay(1, pool, "Coaster:trr", bytes, { { 4, "CoasterName:txt" }, { 8, "CoasterDescription:txt" }, { 0xC, "CoasterIcon:gsi" }, { 0x24, "CoasterSpline:spl" } });
    const auto items = read_tracked_rides(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].ride.has_value());
    const auto& i = items[0].ride->info;
    CHECK(i.type_id == 3);
    CHECK(i.name_text == "CoasterName");
    CHECK(i.description_text == "CoasterDescription");
    CHECK(i.icon == "CoasterIcon");
    CHECK(i.spline == "CoasterSpline");
    CHECK(i.value_a == 10000);
    CHECK(i.value_b == -7500);
    CHECK(i.value_c == 2500);
}

TEST_CASE("version 5 tracked rides give a name, track sections and splines")
{
    Pool pool;
    const auto name = pool.add("Coaster");
    pool.add("Coaster:trr");
    pool.add("Lift:tks");
    pool.add("Path:spl");
    std::vector<std::byte> bytes;
    put(bytes, { 0xFFFFFFFF, name });
    bytes.resize(0x130, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "Coaster:trr", bytes, { { 0xD8, "Lift:tks" }, { 0x114, "Path:spl" }, { 0x120, "Path:spl" } });
    const auto items = read_tracked_rides(overlay, overlay.common());
    REQUIRE(items[0].ride.has_value());
    const auto& r = *items[0].ride;
    CHECK(r.name == "Coaster");
    CHECK(r.track_sections[0].empty());
    CHECK(r.track_sections[1] == "Lift");
    CHECK(r.splines[0] == "Path");
    CHECK(r.splines[3] == "Path");
    CHECK(r.info.name_text.empty());
}

TEST_CASE("later tracked rides point to their attraction fields")
{
    for (const std::uint32_t version : { 4u, 5u })
    {
        CAPTURE(version);
        Pool pool;
        pool.add("Coaster:trr");
        pool.add("CoasterName:txt");
        pool.add("CoasterIcon:gsi");
        const std::uint32_t skip = version == 4 ? 0x3C : 0x44; // from the pointed-at block to the fields
        const std::uint32_t block = 0x140;                     // within the record, past its fixed part
        std::vector<std::byte> bytes(block + skip, std::byte { 0 });
        put(bytes, { version == 4 ? 512u + 3 : 768u + 3, 0, 0, 0 });
        bytes.resize(block + skip + 0x28, std::byte { 0 });
        const std::uint32_t at = pool.base();
        auto word = [&](std::size_t offset, std::uint32_t value) {
            std::vector<std::byte> w;
            put(w, { value });
            std::copy(w.begin(), w.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        };
        word(0x12C, at + block);
        const auto overlay = make_record_overlay(version, pool, "Coaster:trr", bytes, { { block + skip + 4, "CoasterName:txt" }, { block + skip + 0xC, "CoasterIcon:gsi" } });
        const auto items = read_tracked_rides(overlay, overlay.common());
        REQUIRE(items[0].ride.has_value());
        const auto& i = items[0].ride->info;
        CHECK(i.type_id == 3);
        CHECK(i.name_text == "CoasterName");
        CHECK(i.icon == "CoasterIcon");
    }
}
