// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/coaster.hpp"

#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation
{

    namespace
    {
        constexpr float kGravity = 9.81f;
        // Our model's losses, picked so the laps of saved parks' tested coasters come out near their test times.
        constexpr float kRolling = 0.003f;   ///< Of the train's weight
        constexpr float kAir = 0.0004f;      ///< Times speed squared, per metre of the train's mass
        constexpr float kStep = 0.05f;       ///< Seconds per part step, short enough for the steepest drops
        constexpr float kStuck = 4;          ///< Seconds all but stopped before the track's tyres take over
        constexpr float kLaunchLeast = 0.5f; ///< Metres of platform left that a launch is still sized over
        constexpr float kBuildUp = 3;        ///< Passes a swinging train's pushes take to build up to full, and to die down
        constexpr float kBounce = 0.8f;      ///< Each push back up a tower is this much of the one before
        constexpr float kTopHold = 3;        ///< Seconds a drop tower's train is held at the top

        std::size_t point_at(const Coaster& c, float d)
        {
            const auto after = std::ranges::upper_bound(c.distance, d);
            return static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(after - c.distance.begin() - 1, 0, static_cast<std::ptrdiff_t>(c.distance.size()) - 2));
        }

        bool two_ends(const Coaster& c)
        {
            return c.shuttle != Shuttle::None || !c.closed;
        }

        /// `d` brought round onto the track, or held at its ends when it has two.
        float wrap(const Coaster& c, float d)
        {
            const float length = c.length();
            if (two_ends(c))
                return std::clamp(d, 0.0f, length);
            return std::fmod(std::fmod(d, length) + length, length);
        }

        /// The track's height `d` along, round again past either end of a circuit.
        float height_at(const Coaster& c, float d)
        {
            d = wrap(c, d);
            const std::size_t i = point_at(c, d);
            const float span = c.distance[i + 1] - c.distance[i];
            return std::lerp(c.height[i], c.height[i + 1], span > 0 ? (d - c.distance[i]) / span : 0.0f);
        }
    } // namespace

    glm::vec3 track_point(const Coaster& c, float d)
    {
        if (c.distance.size() < 2 || c.points.size() != c.distance.size())
            return { 0, c.distance.size() < 2 ? 0 : height_at(c, d), 0 };
        d = wrap(c, d);
        const std::size_t i = point_at(c, d);
        const float span = c.distance[i + 1] - c.distance[i];
        return glm::mix(c.points[i], c.points[i + 1], span > 0 ? (d - c.distance[i]) / span : 0.0f);
    }

    glm::vec3 track_up(const Coaster& c, float d)
    {
        if (c.distance.size() < 2 || c.ups.size() != c.distance.size())
            return { 0, 1, 0 };
        d = wrap(c, d);
        const std::size_t i = point_at(c, d);
        const float span = c.distance[i + 1] - c.distance[i];
        const glm::vec3 up = glm::mix(c.ups[i], c.ups[i + 1], span > 0 ? (d - c.distance[i]) / span : 0.0f);
        return glm::length(up) > 1e-6f ? glm::normalize(up) : glm::vec3(0, 1, 0);
    }

    glm::vec3 track_way(const Coaster& c, float d)
    {
        if (c.distance.size() < 2 || c.points.size() != c.distance.size())
            return { 0, 0, 1 };
        const std::size_t i = point_at(c, wrap(c, d));
        const glm::vec3 run = c.points[i + 1] - c.points[i];
        return glm::length(run) > 0 ? glm::normalize(run) : glm::vec3(0, 0, 1);
    }

    Felt felt(const Coaster& c)
    {
        // The track's way and its bend from points a little either side, so the corners between its points do
        // not jolt: the turn's pull into the bend at the train's speed. A push along the track is felt along
        // it, so neither up through the seats nor across them.
        constexpr float kReach = 2; ///< Metres either side
        const glm::vec3 here = track_point(c, c.at);
        const glm::vec3 ahead = track_point(c, c.at + kReach);
        const glm::vec3 behind = track_point(c, c.at - kReach);
        const glm::vec3 chord = ahead - behind;
        const glm::vec3 way = glm::length(chord) > 0 ? glm::normalize(chord) : track_way(c, c.at);
        const glm::vec3 bend = (ahead - 2.0f * here + behind) / (kReach * kReach);
        // What riders feel: the train's acceleration less gravity's, up through their seats and across them.
        const glm::vec3 pull = bend * c.speed * c.speed + glm::vec3(0, kGravity, 0);
        const glm::vec3 lean = glm::cross(track_up(c, c.at), way);
        const glm::vec3 across = glm::length(lean) > 1e-6f ? glm::normalize(lean) : glm::vec3(1, 0, 0);
        const glm::vec3 up = glm::cross(way, across);
        return { .vertical = glm::dot(pull, up) / kGravity, .lateral = glm::dot(pull, across) / kGravity, .slope = way.y };
    }

    void record(LapRecorder& lap, const Coaster& c, float seconds)
    {
        if (seconds <= 0)
            return;
        const Felt forces = felt(c);
        const glm::vec3 here = track_point(c, c.at);
        lap.seconds += seconds;
        lap.speed_sum += std::abs(c.speed) * seconds;
        lap.height_sum += here.y * seconds;
        lap.vertical_sum += forces.vertical * kGravity * seconds;
        lap.lateral_sum += forces.lateral * kGravity * seconds;
        constexpr float kSample = 1.0f / static_cast<float>(save::kGraphSamplesPerSecond);
        if (lap.seconds < kSample)
            return;
        lap.speed.push_back(lap.speed_sum / lap.seconds);
        lap.altitude.push_back(lap.height_sum / lap.seconds);
        lap.vertical.push_back(lap.vertical_sum / lap.seconds);
        lap.lateral.push_back(lap.lateral_sum / lap.seconds);
        lap.seconds = 0;
        lap.speed_sum = lap.height_sum = lap.vertical_sum = lap.lateral_sum = 0;
    }

    save::RideGraphs end_lap(LapRecorder& lap)
    {
        const auto newest_first = [](const std::vector<float>& v) {
            const std::size_t kept = std::min(v.size(), kLapSamples);
            return std::vector<float>(v.rbegin(), v.rbegin() + static_cast<std::ptrdiff_t>(kept));
        };
        save::RideGraphs out { .speed = newest_first(lap.speed), .altitude = newest_first(lap.altitude), .vertical_g = newest_first(lap.vertical), .lateral_g = newest_first(lap.lateral) };
        lap = { };
        return out;
    }

    std::vector<Wreck> thrown_cars(const Coaster& c, random::RandomState& dice)
    {
        const auto spread = [&] { return static_cast<float>(random::between(dice, -1.0, 1.0)); };
        const std::size_t cars = static_cast<std::size_t>(std::max(c.cars, 1));
        const float car = c.train_length / static_cast<float>(cars);
        std::vector<Wreck> thrown;
        for (std::size_t k = 0; k < cars; ++k)
        {
            const float d = c.at - (static_cast<float>(k) + 0.5f) * car;
            const glm::vec3 spin = glm::vec3(spread(), spread(), spread()) * kTuning.crashes.tumble;
            const glm::vec3 from = track_point(c, d);
            thrown.push_back({ .at = from, .from = from, .velocity = track_way(c, d) * c.speed, .spin = spin, .ghost = c.ghost });
        }
        return thrown;
    }

    bool chain_at(const Coaster& c, float at)
    {
        return c.distance.size() >= 2 && c.length() > 0 && c.kinds[point_at(c, wrap(c, at))] == Stretch::Chain;
    }

    bool on_chain(const Coaster& c)
    {
        return chain_at(c, c.at) || chain_at(c, c.at - c.train_length);
    }

    bool drive(Coaster& c, float seconds, float room)
    {
        if (!c.running || c.distance.size() < 2 || c.length() <= 0)
            return false;
        const bool open = c.shuttle != Shuttle::None;
        const bool tower = c.shuttle == Shuttle::Tower || c.shuttle == Shuttle::Drop;
        const auto station_at = [&](float d) { return c.kinds[point_at(c, wrap(c, d))] == Stretch::Station; };
        // The push a shuttle's train gets as it sets off (pass 0) and on each pass after, before its last: going up
        // for a tower, the way it is moving for a swing.
        const auto push_on = [&](int pass, float way) {
            const float full = c.launch_speed.value_or(0);
            const auto k = static_cast<float>(pass);
            switch (c.shuttle)
            {
                case Shuttle::Back:
                    return pass == 0 ? -full : 0.0f;
                case Shuttle::Swing:
                    return std::copysign(full * std::min({ 1.0f, (k + 1) / kBuildUp, (static_cast<float>(c.passes) - k) / kBuildUp }), way);
                case Shuttle::Tower:
                case Shuttle::Drop:
                    return full * std::pow(kBounce, k);
                case Shuttle::None:
                    break;
            }
            return 0.0f;
        };
        if (open && c.travelled == 0)
        {
            c.passed = 0;
            c.push = push_on(0, 1);
            c.pushed = 0;
            c.held = 0;
        }
        const int steps = static_cast<int>(std::ceil(seconds / kStep));
        for (int step = 0; step < steps; ++step)
        {
            const float dt = std::min(seconds - static_cast<float>(step) * kStep, kStep);
            const Stretch under = c.kinds[point_at(c, c.at)];
            const bool chained = on_chain(c);
            // Its last time in, a shuttle's train is taken by the station's tyres as soon as either end is on it.
            const bool home = open && c.passed >= c.passes && (station_at(c.at) || station_at(c.at - c.train_length));
            if (c.fixed_speed)
                c.speed = *c.fixed_speed;
            else
            {
                // Pulled down the slope between the train's ends, held back by rolling and the air.
                const float back = c.at - c.train_length;
                const float slope = (height_at(c, c.at) - height_at(c, back)) / c.train_length;
                c.speed += (-kGravity * slope - kGravity * kRolling - kAir * c.speed * std::abs(c.speed)) * dt;
                // A shuttle's chain carries the train either way, the way it is going.
                if (chained)
                    c.speed = open && c.speed < 0 ? std::min(c.speed, -c.lift_speed) : std::max(c.speed, c.lift_speed);
                else if (home)
                    c.speed = std::copysign(c.station_speed, c.start - c.at);
                else if (under == Stretch::Station)
                {
                    // A shuttle's train rolls through the station on its passes before the last.
                    if (!open)
                        c.speed = c.station_speed;
                }
                else if (under == Stretch::Brake)
                {
                    // shortcut: brakes take the speed down at once, upgrade if a train should slow along them.
                    const std::size_t at = point_at(c, c.at);
                    // A brake set to nothing would stop the train for good; it lets it through.
                    if (at < c.brake_speeds.size() && c.brake_speeds[at] > 0)
                        c.speed = std::min(c.speed, c.brake_speeds[at]);
                }
                else
                {
                    // shortcut: a train stalled in a dip is pushed on as if by tyres until it outruns them, upgrade if
                    // valleys should roll it back for good.
                    const float pace = open ? std::abs(c.speed) : c.speed;
                    if (pace < 0.1f)
                        c.slow += dt;
                    if (c.slow > kStuck)
                        c.speed = std::max(c.speed, c.station_speed);
                    if (pace > c.station_speed)
                        c.slow = 0;
                }
                // A drop tower's train is held where its first climb tops out, then let fall.
                if (c.shuttle == Shuttle::Drop && c.passed == 0 && c.push == 0 && c.travelled > 0 && c.speed <= 0 && c.held < kTopHold)
                {
                    c.speed = 0;
                    c.held += dt;
                    continue;
                }
            }
            // Launched: pushed just hard enough to reach the speed as its tail clears the platform it stood on,
            // and on a launch stretch past it as hard as the push from a standing start. A shuttle's push is sized
            // the same way, over a train's length from where it began.
            const bool platform = !c.launched && c.travelled < c.train_length;
            const bool assisted = under == Stretch::Launch || c.kinds[point_at(c, wrap(c, c.at - c.train_length))] == Stretch::Launch;
            float target = 0;
            float left = c.train_length;
            if (open)
            {
                target = c.push;
                left -= c.pushed;
            }
            else if (c.launch_speed && (platform || assisted))
            {
                target = *c.launch_speed;
                left -= platform ? c.travelled : 0;
            }
            const float way = target < 0 ? -1.0f : 1.0f;
            if (target != 0 && !c.fixed_speed && c.speed * way < std::abs(target))
            {
                const float along = c.speed * way;
                const float from = open || platform ? std::max(along, 0.0f) * std::max(along, 0.0f) : 0.0f;
                c.speed = way * std::min(along + (target * target - from) / (2 * std::max(left, kLaunchLeast)) * dt, std::abs(target));
            }
            // shortcut: with other trains on the track a train never rolls back, as if held by anti-rollback
            // dogs everywhere, so it cannot run into the one behind; upgrade when the train behind gives room too.
            if (std::isfinite(room))
                c.speed = std::max(c.speed, 0.0f);
            float moved = c.speed * dt;
            if (moved > room)
            {
                // Held up behind the train in front until it moves on.
                moved = std::max(room, 0.0f);
                c.speed = 0;
            }
            room -= moved;
            // Run off either end of its track, the train crashes there, going as fast as it left.
            if (two_ends(c))
            {
                const float to = c.at + moved;
                const float kept = std::clamp(to, std::min(c.train_length, c.length()), c.length());
                if (kept != to)
                {
                    moved = kept - c.at;
                    c.at = kept;
                    c.crashed = true;
                    c.running = false;
                    return false;
                }
            }
            if (open)
            {
                const float from = c.at;
                c.at += moved;
                c.travelled += std::abs(moved);
                if (c.push != 0 && (c.pushed += std::abs(moved)) >= c.train_length)
                    c.push = 0;
                // Coming into the station, its leading end first: on a tower, only falling back into it.
                const float lead = moved < 0 ? -c.train_length : 0;
                if (moved != 0 && !station_at(from + lead) && station_at(c.at + lead) && (!tower || moved < 0) && ++c.passed < c.passes)
                {
                    if (tower)
                        c.speed = 0; // Braked to a stop on its base, as brakes stop a train here
                    c.push = push_on(c.passed, moved);
                    c.pushed = 0;
                }
                // Back where it set off for the last time, its run is over.
                if (c.passed >= c.passes && (from - c.start) * (c.at - c.start) <= 0 && c.at != from)
                {
                    c.at = c.start;
                    c.travelled = 0;
                    c.speed = 0;
                    c.slow = 0;
                    c.push = 0;
                    c.running = false;
                    return true;
                }
                continue;
            }
            c.at = wrap(c, c.at + moved);
            c.travelled += moved;
            c.launched = c.launched || c.travelled >= c.train_length;
            // At another station, it stops there to let riders off and on.
            if (c.next_stop < c.stops.size() && c.travelled >= c.stops[c.next_stop])
            {
                c.travelled = c.stops[c.next_stop++];
                c.at = wrap(c, c.start + c.travelled);
                c.speed = 0;
                c.slow = 0;
                c.running = false;
                return false;
            }
            // Not yet off the platform, the station holds it from rolling back.
            if (c.travelled < 0 && !c.launched)
            {
                c.at = c.start;
                c.travelled = 0;
                c.speed = 0;
            }
            // Rolled back past where it set off, it is back in the station and stops there, its run over.
            if (c.travelled >= c.length() || c.travelled < 0)
            {
                c.at = c.start;
                c.travelled = 0;
                c.speed = 0;
                c.slow = 0;
                c.launched = false;
                c.running = false;
                c.next_stop = 0;
                return true;
            }
        }
        return false;
    }

    std::optional<std::size_t> standing_at(const Coaster& c)
    {
        if (c.running || c.crashed)
            return std::nullopt;
        return c.travelled == 0 ? 0 : c.next_stop;
    }

    std::optional<float> lap_seconds(Coaster c, float most)
    {
        c.stops.clear();
        c.next_stop = 0;
        c.running = true;
        c.at = c.start;
        c.travelled = 0;
        c.speed = 0;
        constexpr float kTick = 0.25f;
        for (int tick = 1; static_cast<float>(tick) * kTick <= most && c.running; ++tick)
        {
            const float t = static_cast<float>(tick) * kTick;
            if (drive(c, kTick))
                return t;
        }
        return std::nullopt;
    }

} // namespace rct3::simulation
