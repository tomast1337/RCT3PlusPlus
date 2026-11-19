// SPDX-License-Identifier: GPL-3.0-or-later
//
// Things that happen in a park by chance: the kinds, how often each may happen, and rolling for them. Not
// used yet; it stands ready for the simulation. Every rate here is ours to tune, and all start at zero, so
// nothing happens until the simulation sets one.
//
// An event with a rate of r per game hour happens in a step of h hours with probability 1 - e^(-r h): the
// chance does not depend on how the time is cut into steps. Rolls draw from the gameplay stream only.
#pragma once

#include "core/types.hpp"
#include "random/generator.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::simulation
{

    enum class RandomEvent : u8
    {
        RideBreakdown,   ///< A ride stops working until a mechanic fixes it
        WeatherChange,   ///< The weather moves to another kind
        Vandalism,       ///< An angry guest breaks a path item
        GuestArrival,    ///< A group of guests reaches the gate
        ResearchFinding, ///< Research yields a new item
        AnimalEscape,    ///< An animal gets out of its enclosure
        Count_,
    };

    inline constexpr usize kRandomEventCount = static_cast<usize>(RandomEvent::Count_);

    /// The event's name, for logs and saved files.
    [[nodiscard]] std::string_view event_name(RandomEvent event);

    /// How many times each event happens per game hour, on average.
    struct EventRates
    {
        std::array<f64, kRandomEventCount> per_hour { };

        [[nodiscard]] f64& operator[](RandomEvent e)
        {
            return per_hour[static_cast<usize>(e)];
        }
        [[nodiscard]] f64 operator[](RandomEvent e) const
        {
            return per_hour[static_cast<usize>(e)];
        }
    };

    /// The chance that an event of `rate` per hour happens in `hours`.
    [[nodiscard]] f64 chance_in(f64 rate, f64 hours);

    /// The events that happen in the next `hours`, in the order of RandomEvent, each at most once.
    [[nodiscard]] std::vector<RandomEvent> roll_events(random::RandomState& gameplay, const EventRates& rates, f64 hours);

    /// An index into `weights` picked in proportion to its weight. Nothing when no weight is above zero;
    /// negative weights count as zero. For picking what kind of breakdown, which weather, and so on.
    [[nodiscard]] std::optional<usize> pick_weighted(random::RandomState& state, std::span<const f64> weights);

} // namespace rct3::simulation
