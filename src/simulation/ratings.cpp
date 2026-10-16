// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/ratings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace rct3::simulation
{

    namespace
    {
        constexpr float kGravity = 9.81f;
        constexpr float kSmallestDrop = 1; ///< Metres down before a descent counts as a drop

        /// Fewer excitement for very intense rides: a quarter off at each bound passed.
        /// From OpenRCT2, src/openrct2/ride/RideRatings.cpp, RideRatingsApplyIntensityPenalty (bounds 1000, 1100,
        /// 1200, 1320, 1450 in hundredths).
        constexpr std::array kIntensityBounds { 10.0f, 11.0f, 12.0f, 13.2f, 14.5f };
    } // namespace

    Ratings rate(const save::TestResults& t)
    {
        // Our own weights: speed, length, drops, upside-down moments and floating in the air thrill; strong forces
        // shake; sideways forces and turning over sicken.
        const float up = std::max(t.max_positive_vertical / kGravity - 1, 0.0f);
        const float down = std::abs(t.max_negative_vertical) / kGravity;
        const float side = std::abs(t.max_lateral) / kGravity;
        const float inversions = static_cast<float>(std::min(t.inversion_count, 6));
        Ratings r;
        r.excitement = 0.12f * t.max_speed + 0.0015f * t.ride_length + 0.05f * t.highest_drop + 0.25f * inversions + 0.1f * std::min(t.air_time, 10.0f) + 0.15f * static_cast<float>(std::min(t.drop_count, 10));
        r.intensity = 0.6f * up + 0.8f * down + 1.2f * side + 0.06f * t.max_speed + 0.2f * inversions;
        r.nausea = 0.8f * side + 0.3f * up + 0.4f * down + 0.15f * inversions;
        for (const float bound : kIntensityBounds)
        {
            if (r.intensity >= bound)
                r.excitement -= r.excitement / 4;
        }
        return r;
    }

    std::optional<save::TestResults> test_lap(Coaster c, float most)
    {
        c.running = true;
        c.at = c.start;
        c.travelled = 0;
        c.speed = 0;
        save::TestResults t;
        constexpr float kStep = 0.25f;
        // The drop going on: from the highest point since the track last rose.
        float top = 0;
        float last = 0;
        bool first = true;
        const auto height = [&] {
            const auto after = std::ranges::upper_bound(c.distance, c.at);
            const auto i = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(after - c.distance.begin() - 1, 0, static_cast<std::ptrdiff_t>(c.distance.size()) - 1));
            return c.height[i];
        };
        const auto end_drop = [&] {
            if (top - last >= kSmallestDrop)
            {
                ++t.drop_count;
                t.highest_drop = std::max(t.highest_drop, top - last);
            }
        };
        for (float time = kStep; time <= most; time += kStep)
        {
            const bool back = drive(c, kStep);
            t.max_speed = std::max(t.max_speed, std::abs(c.speed));
            const float h = height();
            if (first || h > last)
            {
                if (!first)
                    end_drop();
                top = h;
                first = false;
            }
            last = h;
            if (back)
            {
                end_drop();
                t.ride_time = time;
                t.ride_length = c.length();
                t.average_speed = t.ride_length / time;
                return t;
            }
        }
        return std::nullopt;
    }

} // namespace rct3::simulation
