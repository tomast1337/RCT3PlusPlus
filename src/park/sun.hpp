// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the sun is at a time of day. The park lies at a fixed latitude on a fixed day of a summer, and the
// sun follows the path the textbook formula for those gives: rising in the east soon after five, highest in
// the south at noon, setting in the west before seven. The map's +x is east, +y up and +z north.
//
// Times of day are fractions of a day from midnight, as the day clock keeps them.
#pragma once

#include "math/glm.hpp"

namespace rct3::park
{

    /// How far north of the equator the park lies, in radians.
    inline const float kParkLatitude = glm::radians(40.0f);
    /// How far north of the equator the sun stands on the park's day, in radians: a summer day.
    inline const float kSunDeclination = glm::radians(15.0f);

    /// Unit vector from the ground towards the sun at `time`; below the horizon (y < 0) at night.
    [[nodiscard]] glm::vec3 sun_direction(float time);

    /// The sun's angle above the horizon at `time`, in radians; negative at night.
    [[nodiscard]] float sun_elevation(float time);

} // namespace rct3::park
