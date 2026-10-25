// SPDX-License-Identifier: GPL-3.0-or-later
//
// A train going round a coaster's circuit: our own model of how it moves, not the original game's.
//
// The track is known by its height along its length and by which stretches are lift chains and stations.
// Gravity pulls the train along the slope under its whole length; rolling and the air hold it back. A lift
// chain carries it up at no less than the ride's lift speed, and the station's tyres push it at the ride's
// station speed, slowing it there too when it comes back. Brakes slow it to their speed and no further. A ride
// with a fixed speed moves at that speed everywhere. On a shuttle the train runs back and forth between the
// track's ends, counting its passes through the station. A train that runs off either end of a track with two
// ends, a shuttle's or an unfinished circuit's, crashes. The speeds are the ride's own, as its park keeps them.
//
// Units are metres and seconds.
#pragma once

#include "math/glm.hpp"
#include "random/generator.hpp"
#include "save/model/rides.hpp"
#include "simulation/crash.hpp"

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

    /// How a train on a track with two ends runs back and forth through its station.
    enum class Shuttle : std::uint8_t
    {
        None,  ///< The track is a circuit, gone round once
        Back,  ///< Pushed off backwards, through the station and back to it
        Swing, ///< Pushed on each time through the station, the way it is going, harder and then softer
        Tower, ///< Pushed up, braked as it falls back into the station and pushed up again, softer
        Drop,  ///< As a tower, but held at the top of its first climb before it falls
    };

    struct Coaster
    {
        std::vector<float> distance;     ///< Along the track to each point, from 0
        std::vector<float> height;       ///< Of each point
        std::vector<glm::vec3> points;   ///< Where each point is; may be empty, when only the heights are known
        std::vector<glm::vec3> ups;      ///< Up for the track at each point, leaning with its bank; empty for straight up
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
        int cars = 1;    ///< In the train, each as long as the others
        float start = 0; ///< Where the train's front waits to set off, at the station's end
        /// On a circuit with more than one station, how far on from `start` the train stops at each of the others,
        /// nearest first; it waits at each and goes on round
        std::vector<float> stops { };
        /// Not a circuit: the track has two ends and the train runs back and forth, its speed negative going backwards
        Shuttle shuttle = Shuttle::None;
        int passes = 1; ///< On a shuttle, the times the train comes into the station before it stops there
        /// The track's end joins its start. When it does not, as on one left unfinished, a train going round
        /// runs off its end.
        bool closed = true;
        /// A test train shown while the track is built: it crashes as any train does, but its wrecks hurt no one
        /// and the crash is not the park's
        bool ghost = false;
        // The train
        bool running = false;
        float at = 0;              ///< Distance of the front of the train
        float travelled = 0;       ///< Since it set off
        std::size_t next_stop = 0; ///< Of `stops`, the next it comes to; past one, it stands there until set off
        float speed = 0;           ///< Along the track
        float slow = 0;            ///< Seconds it has been all but stopped away from a chain or station
        bool launched = false;     ///< Its tail has left the platform since it set off, so it is pushed no more there
        // A shuttle's train
        int passed = 0;   ///< Times it has come into the station since it set off
        float push = 0;   ///< The speed it is being pushed to, negative backwards; 0 when it is not
        float pushed = 0; ///< Metres it has moved under that push
        float held = 0;   ///< Seconds it has been held at the top of its climb
        /// Run off an end of its track: stopped running, where it left the track and at the speed it left at
        bool crashed = false;

        [[nodiscard]] float length() const
        {
            return distance.empty() ? 0 : distance.back();
        }
    };

    /// Whether either end of `coaster`'s train is on a lift chain.
    [[nodiscard]] bool on_chain(const Coaster& coaster);

    /// Whether the stretch `at` along `coaster`'s track, round again past either end, is a lift chain.
    [[nodiscard]] bool chain_at(const Coaster& coaster, float at);

    /// The point `d` along `coaster`'s track, round again past the end of a circuit and held at the ends of a
    /// track with two; under its points, at the height, when only that is known.
    [[nodiscard]] glm::vec3 track_point(const Coaster& coaster, float d);

    /// Up for `coaster`'s track `d` along it, leaning with its bank, a unit vector; straight up when not known.
    [[nodiscard]] glm::vec3 track_up(const Coaster& coaster, float d);

    /// The way `coaster`'s track runs `d` along it, a unit vector; +z when it is not known.
    [[nodiscard]] glm::vec3 track_way(const Coaster& coaster, float d);

    /// The wrecks of `coaster`'s crashed train, a car each from the front back, thrown on from where each left the
    /// track at the train's speed, tumbling as `dice` has them.
    [[nodiscard]] std::vector<Wreck> thrown_cars(const Coaster& coaster, random::RandomState& dice);

    /// Moves `coaster`'s running train on by `seconds`, but no further than `room` (the clear track ahead of it,
    /// up to the train in front), where it stops and waits; whether it has come round to its start (on a shuttle,
    /// back to it for the last time), where it stops. At one of its other stops it stops too, no longer running,
    /// but this gives false. A train that runs off an end of a track with two ends is left crashed instead.
    bool drive(Coaster& coaster, float seconds, float room = std::numeric_limits<float>::infinity());

    /// A lap's graphs taken from a train as it runs: a sample each tenth of a second of its speed, height and the
    /// forces its riders feel, each the average over that tenth. The forces are in the track's own frame, so a
    /// banked turn presses riders into their seats rather than sideways.
    struct LapRecorder
    {
        float seconds = 0; ///< Into the sample being taken
        std::optional<float> speed_before { };
        float speed_sum = 0, height_sum = 0, vertical_sum = 0, lateral_sum = 0;
        std::vector<float> speed, altitude, vertical, lateral; ///< Oldest first
    };

    /// Samples a ride's graphs keep in a park file, the lap's last ones.
    inline constexpr std::size_t kLapSamples = 200;

    /// Adds `seconds` of the run of `coaster`'s train, as it is now, to `lap`.
    void record(LapRecorder& lap, const Coaster& coaster, float seconds);

    /// The lap recorded into `lap`, newest sample first as saved and no more than kLapSamples, and starts the
    /// next.
    [[nodiscard]] save::RideGraphs end_lap(LapRecorder& lap);

    /// The station `coaster`'s train stands at, if it stands at one: 0 at its start, k at the k-th of its stops.
    [[nodiscard]] std::optional<std::size_t> standing_at(const Coaster& coaster);

    /// How long a lap of `coaster` takes from a standing start, without stopping on the way, by driving a copy of
    /// it; nothing if the train
    /// does not come round within `most` seconds.
    [[nodiscard]] std::optional<float> lap_seconds(Coaster coaster, float most = 1200);

} // namespace rct3::simulation
