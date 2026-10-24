// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/coaster.hpp"

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

        std::size_t point_at(const Coaster& c, float d)
        {
            const auto after = std::ranges::upper_bound(c.distance, d);
            return static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(after - c.distance.begin() - 1, 0, static_cast<std::ptrdiff_t>(c.distance.size()) - 2));
        }

        /// `d` brought round onto the track.
        float wrap(const Coaster& c, float d)
        {
            const float length = c.length();
            return std::fmod(std::fmod(d, length) + length, length);
        }

        /// The track's height `d` along, round again past either end.
        float height_at(const Coaster& c, float d)
        {
            d = wrap(c, d);
            const std::size_t i = point_at(c, d);
            const float span = c.distance[i + 1] - c.distance[i];
            return std::lerp(c.height[i], c.height[i + 1], span > 0 ? (d - c.distance[i]) / span : 0.0f);
        }
    } // namespace

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
        const int steps = static_cast<int>(std::ceil(seconds / kStep));
        for (int step = 0; step < steps; ++step)
        {
            const float dt = std::min(seconds - static_cast<float>(step) * kStep, kStep);
            const Stretch under = c.kinds[point_at(c, c.at)];
            const bool chained = on_chain(c);
            if (c.fixed_speed)
                c.speed = *c.fixed_speed;
            else
            {
                // Pulled down the slope between the train's ends, held back by rolling and the air.
                const float back = c.at - c.train_length;
                const float slope = (height_at(c, c.at) - height_at(c, back)) / c.train_length;
                c.speed += (-kGravity * slope - kGravity * kRolling - kAir * c.speed * std::abs(c.speed)) * dt;
                if (chained)
                    c.speed = std::max(c.speed, c.lift_speed);
                else if (under == Stretch::Station)
                    c.speed = c.station_speed;
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
                    if (c.speed < 0.1f)
                        c.slow += dt;
                    if (c.slow > kStuck)
                        c.speed = std::max(c.speed, c.station_speed);
                    if (c.speed > c.station_speed)
                        c.slow = 0;
                }
            }
            // Launched: pushed just hard enough to reach the speed as its tail clears the platform it stood on,
            // and on a launch stretch past it as hard as the push from a standing start.
            // shortcut: the launch modes that run back and forth are not pushed, upgrade with shuttle running.
            const bool platform = c.travelled < c.train_length;
            const bool assisted = under == Stretch::Launch || c.kinds[point_at(c, wrap(c, c.at - c.train_length))] == Stretch::Launch;
            if (c.launch_speed && !c.fixed_speed && (platform || assisted) && c.speed < *c.launch_speed)
            {
                const float left = std::max(platform ? c.train_length - c.travelled : c.train_length, kLaunchLeast);
                const float from = platform ? c.speed * c.speed : 0.0f;
                c.speed = std::min(c.speed + (*c.launch_speed * *c.launch_speed - from) / (2 * left) * dt, *c.launch_speed);
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
            c.at = wrap(c, c.at + moved);
            c.travelled += moved;
            if (c.travelled >= c.length())
            {
                c.at = c.start;
                c.travelled = 0;
                c.speed = 0;
                c.slow = 0;
                c.running = false;
                return true;
            }
        }
        return false;
    }

    std::optional<float> lap_seconds(Coaster c, float most)
    {
        c.running = true;
        c.at = c.start;
        c.travelled = 0;
        c.speed = 0;
        constexpr float kTick = 0.25f;
        for (int tick = 1; static_cast<float>(tick) * kTick <= most; ++tick)
        {
            const float t = static_cast<float>(tick) * kTick;
            if (drive(c, kTick))
                return t;
        }
        return std::nullopt;
    }

} // namespace rct3::simulation
