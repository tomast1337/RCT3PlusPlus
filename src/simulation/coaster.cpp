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
        constexpr float kRolling = 0.003f; ///< Of the train's weight
        constexpr float kAir = 0.0004f;    ///< Times speed squared, per metre of the train's mass
        constexpr float kStep = 0.05f;     ///< Seconds per part step, short enough for the steepest drops
        constexpr float kStuck = 4;        ///< Seconds all but stopped before the track's tyres take over

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

    bool drive(Coaster& c, float seconds)
    {
        if (!c.running || c.distance.size() < 2 || c.length() <= 0)
            return false;
        for (float left = seconds; left > 0; left -= kStep)
        {
            const float dt = std::min(left, kStep);
            const Stretch under = c.kinds[point_at(c, c.at)];
            const bool chained = under == Stretch::Chain || c.kinds[point_at(c, wrap(c, c.at - c.train_length))] == Stretch::Chain;
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
            c.at = wrap(c, c.at + c.speed * dt);
            c.travelled += c.speed * dt;
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
        for (float t = kTick; t <= most; t += kTick)
        {
            if (drive(c, kTick))
                return t;
        }
        return std::nullopt;
    }

} // namespace rct3::simulation
