// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cars of tracked rides' trains, as placed pieces. A park keeps every car's place and turn in the world as
// they were when it was saved, so the cars stand there; they do not move yet. A car is drawn with the model
// its train's design names for its slot, in the ride's car colours.
#pragma once

#include "park/object_mesh.hpp"
#include "save/model/rides.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    /// Which car design a car piece shows: one of the up to six of a ride train.
    struct CarDesign
    {
        std::string train; ///< The ride train's name, without its tag
        std::size_t slot = 0;
    };

    /// Where a car piece's car is: its ride, and how far along the track the park kept its wheels.
    struct CarSpot
    {
        std::size_t ride = 0;
        std::size_t train = 0; ///< Of the ride's trains
        float front = 0;       ///< Of the car's front wheels
        float rear = 0;        ///< Of its rear wheels
    };

    struct RideCars
    {
        std::vector<save::PlacedObject> pieces; ///< Ids after `last_id`; overlay and symbol key the model
        std::vector<CarDesign> designs;         ///< By piece
        std::vector<CarSpot> spots;             ///< By piece
        Fitted fitted;                          ///< Where each piece stands, by id
    };

    /// The design slot of each car, front to back, of a train of `cars` passenger cars whose design fills the
    /// slots `filled`. The slots hold its front, second, middle, second-to-last and last car, then a link
    /// coupled between each two cars. A car whose slot is empty is a middle car, and one with no middle car the
    /// front car; with no link the cars are coupled straight.
    [[nodiscard]] std::vector<std::size_t> car_slots(int cars, const std::array<bool, 6>& filled);

    /// The cars of `rides` whose place the park kept, with ids from `last_id` + 1.
    [[nodiscard]] RideCars ride_cars(std::span<const save::Ride> rides, std::uint64_t last_id);

} // namespace rct3::park
