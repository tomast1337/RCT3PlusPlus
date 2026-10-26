// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_cars.hpp"

#include <doctest/doctest.h>
#include <numbers>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("ride cars stand where the park kept them, turned as it kept them")
{
    save::Ride ride;
    ride.car_colours = { 3, 4, 5 };
    save::Train& train = ride.trains.emplace_back();
    train.symbol = "Coaster:rit";
    train.overlay_path = "Cars\\Coaster";
    save::Car& car = train.cars.emplace_back();
    car.design_slot = 2;
    car.position = { 10, 2, -4 };
    car.position_valid = true;
    // A half turn about the vertical.
    car.orientation = { 0, std::numbers::pi_v<float>, 0 };
    train.cars.emplace_back(); // no kept place: left out

    const RideCars cars = ride_cars(std::span(&ride, 1), 100);
    REQUIRE(cars.pieces.size() == 1);
    const save::PlacedObject& piece = cars.pieces[0];
    CHECK(piece.id == 101);
    CHECK(piece.colours == std::array<std::int32_t, 3> { 3, 4, 5 });
    CHECK(cars.designs[0].train == "Coaster");
    CHECK(cars.designs[0].slot == 2);
    const glm::mat4& placed = cars.fitted.at(101);
    const glm::vec4 front = placed * glm::vec4(0, 0, 1, 1);
    CHECK(front.x == doctest::Approx(10));
    CHECK(front.y == doctest::Approx(2));
    CHECK(front.z == doctest::Approx(-5));
}

TEST_CASE("a train's cars take the front, second, middle, second-to-last and last designs, with links between")
{
    using S = std::vector<std::size_t>;
    CHECK(car_slots(4, { true, false, true, false, true, true }) == S { 0, 5, 2, 5, 2, 5, 4 });
    CHECK(car_slots(5, { true, true, true, true, true, false }) == S { 0, 1, 2, 3, 4 });
    CHECK(car_slots(3, { true, false, false, false, true, true }) == S { 0, 5, 0, 5, 4 });
    CHECK(car_slots(3, { true, false, false, false, false, false }) == S { 0, 0, 0 });
    CHECK(car_slots(1, { true, true, true, true, true, true }) == S { 0 });
}
