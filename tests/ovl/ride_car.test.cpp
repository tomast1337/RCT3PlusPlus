// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/ride_car.hpp"

#include "record_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::test;
using rct3::ovl::read_ride_cars;

TEST_CASE("ride cars expose their names, kind, visual, moving part, wheels, wheel turns and axles")
{
    constexpr std::uint32_t sizes[] = { 0, 240, 0, 0, 292, 336 };
    for (std::uint32_t version : { 1u, 4u, 5u })
    {
        CAPTURE(version);
        Pool pool;
        const auto name = pool.add("TestCar");
        const auto display = pool.add("Test Car");
        pool.add("Test:ric");
        pool.add("testcar:svd");
        pool.add("tub:svd");
        pool.add("rimfl:svd");
        pool.add("axler:svd");
        std::vector<std::byte> bytes;
        put(bytes, { name, display, 11, 0 });
        bytes.resize(sizes[version], std::byte { 0 });
        bytes[0xB0] = std::byte { 1 }; // The front left wheel's piece turned half round
        const auto overlay = make_record_overlay(version, pool, "Test:ric", bytes, { { 0x0C, "testcar:svd" }, { 0x14, "tub:svd" }, { 0xAC, "rimfl:svd" }, { 0xC8, "axler:svd" } });
        const auto items = read_ride_cars(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK(items[0].name == "Test");
        REQUIRE(items[0].car.has_value());
        const auto& car = *items[0].car;
        CHECK(car.name == "TestCar");
        CHECK(car.display_name == "Test Car");
        CHECK(car.kind == 11);
        CHECK(car.visual == "testcar");
        CHECK(car.part == "tub");
        CHECK(car.wheels[0].empty());
        CHECK(car.wheels[1] == "rimfl");
        CHECK(car.wheels_turned == std::array { false, true, false, false });
        CHECK(car.axles[0].empty());
        CHECK(car.axles[1] == "axler");
        REQUIRE(car.references.size() == 4);
        CHECK(car.references[1].name == "tub");
        CHECK(car.record.size() == sizes[version]);
    }
}

TEST_CASE("a ride car at the end of its block is unreadable")
{
    Pool pool;
    pool.add("Test:ric");
    std::vector<std::byte> bytes(200, std::byte { 0 });
    const auto overlay = make_record_overlay(1, pool, "Test:ric", bytes);
    const auto items = read_ride_cars(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].car.has_value());
}

TEST_CASE("ride cars expose how their moving parts move, none where the motion is 0")
{
    Pool pool;
    pool.add("Test:ric");
    std::vector<std::byte> bytes;
    bytes.resize(0x18);
    // The first part's block: swings forward and back, held at rest, no fastest turn.
    put(bytes, { 2, bits(300), bits(1.5f), bits(0.25f), bits(-1), bits(0.3f), bits(40), bits(0.05f), bits(0) });
    bytes.resize(0xDC);
    // The second's: spins, at most 4 radians a second, any way round.
    put(bytes, { 4, bits(100), bits(0.5f), 0, bits(4), bits(0.6f), 0, 0, bits(-1) });
    bytes.resize(336);
    const auto overlay = make_record_overlay(5, pool, "Test:ric", bytes);
    const auto items = read_ride_cars(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].car.has_value());
    const auto& moves = items[0].car->moves;
    REQUIRE(moves[0].has_value());
    CHECK(moves[0]->motion == rct3::ovl::PartMotion::SwingsForward);
    CHECK(moves[0]->damping.value == doctest::Approx(1.5f));
    CHECK(moves[0]->centring.value == doctest::Approx(0.25f));
    CHECK_FALSE(moves[0]->fastest.has_value());
    CHECK(moves[0]->pull.value == doctest::Approx(0.3f));
    CHECK(moves[0]->spring == doctest::Approx(40));
    CHECK(moves[0]->friction == doctest::Approx(0.05f));
    REQUIRE(moves[0]->furthest.has_value());
    CHECK(moves[0]->furthest->value == 0);
    REQUIRE(moves[1].has_value());
    CHECK(moves[1]->motion == rct3::ovl::PartMotion::Spins);
    REQUIRE(moves[1]->fastest.has_value());
    CHECK(moves[1]->fastest->value == doctest::Approx(4));
    CHECK(moves[1]->pull.value == doctest::Approx(0.6f));
    CHECK_FALSE(moves[1]->furthest.has_value());

    // A car of one piece holds 0 for each motion.
    Pool one_piece;
    one_piece.add("Test:ric");
    const auto still = make_record_overlay(5, one_piece, "Test:ric", std::vector<std::byte>(336));
    const auto plain = read_ride_cars(still, still.common());
    REQUIRE(plain.size() == 1);
    REQUIRE(plain[0].car.has_value());
    CHECK_FALSE(plain[0].car->moves[0].has_value());
    CHECK_FALSE(plain[0].car->moves[1].has_value());
}
