// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sine, cosine, arctangent, exponential, logarithm and power that give the same bits on every machine and
// compiler. The system's maths library may round these differently in the last bit from one platform to
// another, and one bit is enough for two copies of a park to drift apart. These use only IEEE 754 addition,
// multiplication, division and exact steps (comparisons, sign and exponent changes), which round the same
// everywhere: the argument is reduced to a small range, and a polynomial (its Taylor or arctangent series,
// cut off once the terms fall below a double's precision) is summed in a fixed order. They work in doubles, so
// the float versions come out within one unit in the last place of the system's.
//
// The simulation uses these; drawing may keep the system's.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"

namespace rct3::math
{

    [[nodiscard]] f64 sin(f64 x);
    [[nodiscard]] f64 cos(f64 x);
    [[nodiscard]] f64 atan(f64 t);
    /// The angle of (x, y) from the x axis, -pi to pi, with IEEE 754's answers at zeros and infinities.
    [[nodiscard]] f64 atan2(f64 y, f64 x);
    [[nodiscard]] f64 exp(f64 x);
    /// The natural logarithm.
    [[nodiscard]] f64 log(f64 x);
    /// `x` to the power `y`; a negative `x` only to a whole power.
    [[nodiscard]] f64 pow(f64 x, f64 y);

    [[nodiscard]] f32 sin(f32 x);
    [[nodiscard]] f32 cos(f32 x);
    [[nodiscard]] f32 atan2(f32 y, f32 x);
    [[nodiscard]] f32 exp(f32 x);
    [[nodiscard]] f32 log(f32 x);
    [[nodiscard]] f32 pow(f32 x, f32 y);

    /// A turn by `angle` radians about `axis`, as glm::rotate gives it from the identity, by these sine and cosine.
    [[nodiscard]] glm::mat4 rotation(f32 angle, glm::vec3 axis);
    /// The same turn as a quaternion, as glm::angleAxis gives it for a unit `axis`.
    [[nodiscard]] glm::quat angle_axis(f32 angle, glm::vec3 axis);

} // namespace rct3::math
