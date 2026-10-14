// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sky_look.hpp"

#include "park/sky_mesh.hpp"
#include "park/sun.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rct3::park
{

    namespace
    {

        struct ColumnAt
        {
            float height; ///< The sun's, in degrees
            float column;
        };

        // By the sun's height, from low to high. Lower than the first and higher than the last hold those.
        constexpr std::array<ColumnAt, 5> kMorning { {
            { -12.0f, kNightColumn },
            { -3.0f, (kNightColumn + kDawnColumn) / 2 },
            { 2.0f, kDawnColumn },
            { 10.0f, kMorningColumn },
            { 40.0f, kMiddayColumn },
        } };
        constexpr std::array<ColumnAt, 5> kEvening { {
            { -12.0f, kLateDuskColumn },
            { -6.0f, kDuskColumn },
            { 0.0f, kSunsetColumn },
            { 12.0f, kAfternoonColumn },
            { 40.0f, kMiddayColumn },
        } };

        float column_for(std::span<const ColumnAt> points, float height)
        {
            if (height <= points.front().height)
                return points.front().column;
            for (std::size_t i = 1; i < points.size(); ++i)
            {
                if (height <= points[i].height)
                {
                    const float t = (height - points[i - 1].height) / (points[i].height - points[i - 1].height);
                    return glm::mix(points[i - 1].column, points[i].column, t);
                }
            }
            return points.back().column;
        }

    } // namespace

    float sky_column(float time)
    {
        const float height = glm::degrees(sun_elevation(time));
        const float day = time - std::floor(time);
        return column_for(day < 0.5f ? std::span<const ColumnAt>(kMorning) : std::span<const ColumnAt>(kEvening), height);
    }

    float sky_darkness(float time)
    {
        const float height = glm::degrees(sun_elevation(time));
        const float t = std::clamp((kStarsAppear - height) / (kStarsAppear - kStarsFull), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }

    glm::vec4 band_colour(std::span<const glm::vec4> band, float column)
    {
        if (band.empty())
            return glm::vec4(0.0f);
        const float wrapped = column - std::floor(column);
        const auto at = std::min(static_cast<std::size_t>(wrapped * static_cast<float>(band.size())), band.size() - 1);
        return band[at];
    }

} // namespace rct3::park
