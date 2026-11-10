// SPDX-License-Identifier: GPL-3.0-or-later
//
// A train going round a coaster's circuit: our own model of how it moves, not the original game's.
//
// The track is known by its height along its length and by which stretches are lift chains and stations.
// Gravity pulls the train along the slope under its whole length; rolling and the air hold it back. A lift
// chain carries it up at no less than the ride's lift speed, and the station's tyres push it at the ride's
// station speed, slowing it there too when it comes back. Brakes slow it down to their speed and no further,
// each car over one braking it, so a short run of brakes may not bring a fast train all the way down. Block brakes
// do the same and push a slower train up to their speed; one held for the block ahead brakes it to a stop. A ride
// with a fixed speed moves at that speed everywhere. Boosters' jets shove a car on hard while it is slower than their
// speed, as a water coaster's climbs have them. A pool slide's car is not carried off its platform but eased off at a
// walk, the slope taking it from there. In its turns an open slide's car swings up the trough's wall, as far as the
// turn's pull outweighs gravity and the wall's friction; swung up past the rims it flies out and crashes. On a shuttle the train runs back and forth between the
// track's ends, counting its passes through the station. A train that runs off either end of a track with two
// ends, a shuttle's or an unfinished circuit's, crashes. The speeds are the ride's own, as its park keeps them.
//
// Units are metres and seconds.
#pragma once

#include "math/glm.hpp"
#include "random/generator.hpp"
#include "save/model/rides.hpp"
#include "simulation/crash.hpp"

#include <array>
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
        BlockBrake, ///< Slows or pushes the train to the stretch's brake speed; ends a block, where a train may be held
        Launch,     ///< Goes on pushing a launched train as hard as its platform began to
        Brake,      ///< Slows the train to the stretch's brake speed
        Booster,    ///< Jets push the cars over it up to the stretch's brake speed, hard, the way they are going
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

    /// How a pool slide's car rides its open trough, as its car file has it: a turn swings it up the trough's wall,
    /// gravity swings it back down, and taken too fast it goes over the rim.
    struct Trough
    {
        float damping = 0;  ///< Per second, how fast its swing dies down
        float centring = 0; ///< Per second, how fast it is drawn back to the trough's middle
        float friction = 0; ///< Of the car on the wall, held back as it swings by this much of the weight pressing it there
        /// Metres from the middle of its swing, on the track's line, down to the car: its shape's pivot height. A
        /// body slide's car's, matching one under way in a saved park, unless the car's shape gives its own.
        float length = 0.77f;
        float pivot = 0; ///< Metres up from where the car is drawn to the middle of its swing on the car's shape
    };

    /// Of a car run off a pool slide's open end: flying on as fast as it left, falling, then afloat where it came
    /// down, drifting to a stop, till it sinks out of sight.
    struct Flight
    {
        glm::vec3 from { };     ///< Where it left the track
        glm::vec3 way { };      ///< The track's way there
        glm::vec3 at { };       ///< Where it is now
        glm::vec3 velocity { }; ///< Metres a second
        float water = 0;        ///< Height it comes down at: the pool's water, or the ground with no pool under it
        float afloat = -1;      ///< Seconds since it came down; below 0 while it flies
        float off = 0;          ///< Seconds since it left the track
        float speed = 0;        ///< Metres a second it left the track at, along `way`
        /// Metres across the ground from `from` it goes at most: where the far wall of the pool it comes down in
        /// stops it, going on only down.
        float reach = std::numeric_limits<float>::infinity();
    };

    /// Seconds after it left the track a car off a slide's end is gone. Measured: a capture of the original game,
    /// where such a car is back on its track ten seconds after it left it.
    inline constexpr float kGone = 10;

    /// Moves `flight` on by `seconds`: flying it falls, and comes down on its water; afloat it drifts on slowing
    /// and sinks in its last second. False once kGone seconds have passed since it left the track and it is gone.
    /// Where it is comes out the same however its time is cut into steps, so a flight can be flown again from its
    /// start to any moment of it.
    bool fly(Flight& flight, float seconds);

    /// Moves a car drawn at `flight`'s start, as it left the track, to where the flight has it: nose along its
    /// way while it flies, level once afloat.
    [[nodiscard]] glm::mat4 flown(const Flight& flight, const glm::mat4& place);

    struct Coaster
    {
        std::vector<float> distance;     ///< Along the track to each point, from 0
        std::vector<float> height;       ///< Of each point
        std::vector<glm::vec3> points;   ///< Where each point is; may be empty, when only the heights are known
        std::vector<glm::vec3> ups;      ///< Up for the track at each point, leaning with its bank; empty for straight up
        std::vector<Stretch> kinds;      ///< Of the stretch from each point to the next
        std::vector<float> brake_speeds; ///< Of each brake stretch, by its first point; 0 or less elsewhere
        /// How hard brakes slow a train that is wholly on them, in m/s²: their force over the train's mass. A
        /// train partly on them is slowed as much less as it has fewer cars over them.
        float brake_pull = 10;
        /// How hard block brakes push a train wholly on them, in m/s², while it is slower than their speed.
        float boost_pull = 20;
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
        /// Of a track with two ends, the far end is a pool slide's: a train running off it drops its riders
        /// into the pool and is gone, not crashed, till it is back at the station.
        bool splash_out = false;
        /// A test train shown while the track is built: it crashes as any train does, but its wrecks hurt no one
        /// and the crash is not the park's
        bool ghost = false;
        /// Of a pool slide's car, how it swings in its trough; nothing for every other train.
        std::optional<Trough> trough;
        std::vector<bool> covered { };               ///< By point: the stretch from it is a tube, which no car goes out of
        std::vector<bool> steady { };                ///< By point: on the stretch from it a slide's car swings by its weight alone
        std::vector<float> drop_offs { };            ///< As park::track::TrackCourse::drop_offs
        std::vector<std::array<float, 2>> wades { }; ///< As park::track::TrackCourse::wades: in them the pool's water carries the car on slowly
        /// What holds it back as it goes: rolling, as this much of its weight on the track, and the air, as this
        /// much of its speed squared, in m/s² for each (m/s)². Those of a train in saved parks' tested coasters
        /// unless its train's file gives its own.
        float rolling = 0.003f;
        float air = 0.0004f;
        // The train
        bool running = false;
        float at = 0;              ///< Distance of the front of the train
        float was_at = 0;          ///< Where the front was when the last step began, for drawing between steps
        float travelled = 0;       ///< Since it set off
        std::size_t next_stop = 0; ///< Of `stops`, the next it comes to; past one, it stands there until set off
        float speed = 0;           ///< Along the track
        float slow = 0;            ///< Seconds it has been all but stopped away from a chain or station
        bool launched = false;     ///< Its tail has left the platform since it set off, so it is pushed no more there
        float lean = 0;            ///< Of a slide's car, radians it has swung up the trough's wall from the bottom, to its right
        float lean_rate = 0;       ///< How fast that swing is changing, in radians a second
        // A shuttle's train
        int passed = 0;   ///< Times it has come into the station since it set off
        float push = 0;   ///< The speed it is being pushed to, negative backwards; 0 when it is not
        float pushed = 0; ///< Metres it has moved under that push
        float held = 0;   ///< Seconds it has been held at the top of its climb
        /// Run off an end of its track, or a slide's car over its trough's rim: stopped running, where it left the
        /// track and at the speed it left at
        bool crashed = false;
        /// Run off the end of a pool slide: off the track, where it left it and at the speed it left at
        bool splashed = false;
        std::optional<Flight> flight; ///< Of a car run off the end of a pool slide, till it is gone

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
    /// but this gives false. A train that runs off an end of a track with two ends is left crashed instead, or
    /// off the end of a pool slide left splashed, which counts as coming round.
    bool drive(Coaster& coaster, float seconds, float room = std::numeric_limits<float>::infinity());

    /// What riders of a train feel at its front, from the bend of the track at its speed and gravity: in g, up
    /// through their seats and across them, and the track's way there (1 straight up, -1 straight down).
    struct Felt
    {
        float vertical = 1;
        float lateral = 0;
        float slope = 0;
    };
    [[nodiscard]] Felt felt(const Coaster& coaster);

    /// A lap's graphs taken from a train as it runs: a sample each tenth of a second of its speed, height and the
    /// forces its riders feel, each the average over that tenth. The forces are in the track's own frame, so a
    /// banked turn presses riders into their seats rather than sideways.
    struct LapRecorder
    {
        float seconds = 0; ///< Into the sample being taken
        float speed_sum = 0, height_sum = 0, vertical_sum = 0, lateral_sum = 0;
        std::vector<float> speed, altitude, vertical, lateral; ///< Oldest first
    };

    /// Samples a ride's graphs keep in a park file, the lap's last ones.
    inline constexpr std::size_t kLapSamples = 200;

    /// Adds `seconds` of the run of `coaster`'s train, as it is now, to `lap`.
    void record(LapRecorder& lap, const Coaster& coaster, float seconds);

    /// The lap recorded into `lap`, newest sample first as saved and no more than kLapSamples, and starts the
    /// next.
    [[nodiscard]] save::model::RideGraphs end_lap(LapRecorder& lap);

    /// Where `coaster`'s front is drawn `along` (0 to 1) through its last step, between where it began and ended it.
    [[nodiscard]] float between_drives(const Coaster& coaster, float along);

    /// Metres short of a place on the track that a train's front counts as there: so a train held up by the block
    /// ahead right at the station's end has come in, lets its riders off and loads, and a block's end is reached.
    inline constexpr float kArrived = 0.01f;

    /// The station `coaster`'s train stands at, if it stands at one: 0 at its start, k at the k-th of its stops.
    [[nodiscard]] std::optional<std::size_t> standing_at(const Coaster& coaster);

    /// The station `coaster`'s running train is held up in by the train ahead, short of where it stops there, as
    /// standing_at numbers them. Its riders get off there, as they would at its end. Nothing while it moves, or when
    /// held anywhere else.
    [[nodiscard]] std::optional<std::size_t> held_in_station(const Coaster& coaster);

    /// How long a lap of `coaster` takes from a standing start, without stopping on the way, by driving a copy of
    /// it; nothing if the train
    /// does not come round within `most` seconds.
    [[nodiscard]] std::optional<float> lap_seconds(Coaster coaster, float most = 1200);

} // namespace rct3::simulation
