// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cars of tracked rides' trains, as placed pieces. A park keeps every car's place and turn in the world as
// they were when it was saved, so the cars stand there; they do not move yet. A car is drawn with the model
// its train's design names for its slot, in the ride's car colours.
#pragma once

#include "park/object_mesh.hpp"
#include "save/model/rides.hpp"

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

    /// Where a car piece's car is: its ride, and how far along the track the park kept it.
    struct CarSpot
    {
        std::size_t ride = 0;
        float distance = 0;
    };

    struct RideCars
    {
        std::vector<save::PlacedObject> pieces; ///< Ids after `last_id`; overlay and symbol key the model
        std::vector<CarDesign> designs;         ///< By piece
        std::vector<CarSpot> spots;             ///< By piece
        Fitted fitted;                          ///< Where each piece stands, by id
    };

    /// The cars of `rides` whose place the park kept, with ids from `last_id` + 1.
    [[nodiscard]] RideCars ride_cars(std::span<const save::Ride> rides, std::uint64_t last_id);

} // namespace rct3::park
