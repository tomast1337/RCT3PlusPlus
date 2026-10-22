// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which attraction each placed object belongs to, so a click on a piece of track, a ride's building or a
// stall finds the attraction it is part of. Track pieces name the object that carries them; rides, stalls
// and facilities name the placement their objects hang from, and so do the entrances and exits of stations.
#pragma once

#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace rct3::park
{

    struct AttractionRef
    {
        enum class Kind : std::uint8_t
        {
            Ride,  ///< Tracked, flat or a facility: an index into RideModel::rides
            Stall, ///< An index into RideModel::stalls
        };
        Kind kind = Kind::Ride;
        std::size_t index = 0;

        friend bool operator==(const AttractionRef&, const AttractionRef&) = default;
    };

    /// The attraction of each object that belongs to one, by object id.
    using AttractionObjects = std::unordered_map<std::uint64_t, AttractionRef>;

    [[nodiscard]] AttractionObjects attraction_objects(const save::RideModel& rides, std::span<const save::PlacedObject> objects);

    /// The objects that stand for the same thing as one of the objects at `of` (places in `objects`): of the same
    /// asset at the same place and turn, one hanging from the other. Saves may keep a ride's building, entrance and
    /// exit twice so; what draws one moving must leave out its twin.
    [[nodiscard]] std::vector<std::size_t> twins(std::span<const save::PlacedObject> objects, std::span<const std::size_t> of);

} // namespace rct3::park
