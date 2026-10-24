// SPDX-License-Identifier: GPL-3.0-or-later
//
// A train going round a coaster's circuit: our own model of how it moves, not the original game's.
//
// The track is known by its height along its length and by which stretches are lift chains and stations.
// Gravity pulls the train along the slope under its whole length; rolling and the air hold it back. A lift
// chain carries it up at no less than the ride's lift speed, and the station's tyres push it at the ride's
// station speed, slowing it there too when it comes back. Brakes slow it to their speed and no further. A ride
// with a fixed speed moves at that speed
// everywhere. The speeds are the ride's own, as its park keeps them.
//
// Units are metres and seconds.
#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace rct3::simulation
{

    enum class Stretch : std::uint8_t
    {
        Plain,
        Chain,
        Station,
        BlockBrake, ///< Rolled over as plain track; ends a block
        Launch,     ///< Goes on pushing a launched train as hard as its platform began to
        Brake,      ///< Slows the train to the stretch's brake speed
    };

    struct Coaster
    {
        std::vector<float> distance;     ///< Along the track to each point, from 0
        std::vector<float> height;       ///< Of each point
        std::vector<Stretch> kinds;      ///< Of the stretch from each point to the next
        std::vector<float> brake_speeds; ///< Of each brake stretch, by its first point; unread elsewhere
        float lift_speed = 2;
        float station_speed = 2;
        std::optional<float> fixed_speed;
        /// In a launch mode, the speed the train is pushed up to by the time its last car leaves the platform,
        /// and on along any launch stretches while it is still slower.
        std::optional<float> launch_speed;
        /// Trains keep a block apart: one waits at the end of a station, lift or block brake while the block
        /// beyond holds another.
        bool blocks = false;
        float train_length = 10;
        float start = 0; ///< Where the train's front waits to set off, at the station's end
        // The train
        bool running = false;
        float at = 0;        ///< Distance of the front of the train
        float travelled = 0; ///< Since it set off
        float speed = 0;     ///< Along the track
        float slow = 0;      ///< Seconds it has been all but stopped away from a chain or station

        [[nodiscard]] float length() const
        {
            return distance.empty() ? 0 : distance.back();
        }
    };

    /// Whether either end of `coaster`'s train is on a lift chain.
    [[nodiscard]] bool on_chain(const Coaster& coaster);

    /// Whether the stretch `at` along `coaster`'s track, round again past either end, is a lift chain.
    [[nodiscard]] bool chain_at(const Coaster& coaster, float at);

    /// Moves `coaster`'s running train on by `seconds`, but no further than `room` (the clear track ahead of it,
    /// up to the train in front), where it stops and waits; whether it has come round to its start, where it
    /// stops.
    bool drive(Coaster& coaster, float seconds, float room = std::numeric_limits<float>::infinity());

    /// How long a lap of `coaster` takes from a standing start, by driving a copy of it; nothing if the train
    /// does not come round within `most` seconds.
    [[nodiscard]] std::optional<float> lap_seconds(Coaster coaster, float most = 1200);

} // namespace rct3::simulation
