// SPDX-License-Identifier: GPL-3.0-or-later
#include "math/portable.hpp"

#include "core/types.hpp"
#include "math/glm.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <doctest/doctest.h>
#include <limits>

using namespace rct3;

namespace
{
    /// How many floats lie between `a` and `b` (both finite, of one sign).
    u32 floats_apart(f32 a, f32 b)
    {
        const auto x = std::bit_cast<i32>(a);
        const auto y = std::bit_cast<i32>(b);
        return static_cast<u32>(x > y ? x - y : y - x);
    }

    /// `count` evenly spaced values from `from` to `to`.
    template<typename Body> void across(f64 from, f64 to, i32 count, const Body& body)
    {
        for (i32 i = 0; i <= count; ++i)
            body(from + (to - from) * static_cast<f64>(i) / static_cast<f64>(count));
    }
} // namespace

TEST_CASE("the portable sine and cosine are within a float's last place of the system's")
{
    u32 sin_most = 0;
    u32 cos_most = 0;
    f64 wide_most = 0;
    across(-100, 100, 200000, [&](f64 x) {
        const auto f = static_cast<f32>(x);
        sin_most = std::max(sin_most, floats_apart(math::sin(f), std::sin(f)));
        cos_most = std::max(cos_most, floats_apart(math::cos(f), std::cos(f)));
        wide_most = std::max({ wide_most, std::abs(math::sin(x) - std::sin(x)), std::abs(math::cos(x) - std::cos(x)) });
    });
    CHECK(sin_most <= 1);
    CHECK(cos_most <= 1);
    CHECK(wide_most < 1e-15);
    CHECK(math::sin(0.0) == 0);
    CHECK(math::cos(0.0) == 1);
}

TEST_CASE("the portable arctangent is within a float's last place of the system's, with its answers at zeros and infinities")
{
    u32 most = 0;
    across(-50, 50, 400, [&](f64 y) {
        across(-50, 50, 400, [&](f64 x) {
            const auto fy = static_cast<f32>(y);
            const auto fx = static_cast<f32>(x);
            if (fx != 0 || fy != 0)
                most = std::max(most, floats_apart(math::atan2(fy, fx), std::atan2(fy, fx)));
        });
    });
    CHECK(most <= 1);
    constexpr f64 kInfinity = std::numeric_limits<f64>::infinity();
    for (const f64 y : { 0.0, -0.0, 1.0, -1.0, kInfinity, -kInfinity })
        for (const f64 x : { 0.0, -0.0, 1.0, -1.0, kInfinity, -kInfinity })
        {
            CAPTURE(y);
            CAPTURE(x);
            CHECK(std::bit_cast<u64>(math::atan2(y, x)) == std::bit_cast<u64>(std::atan2(y, x)));
        }
}

TEST_CASE("the portable exponential, logarithm and power are within a float's last place of the system's")
{
    u32 exp_most = 0;
    u32 log_most = 0;
    u32 pow_most = 0;
    across(-80, 80, 100000, [&](f64 x) {
        const auto f = static_cast<f32>(x);
        exp_most = std::max(exp_most, floats_apart(math::exp(f), std::exp(f)));
    });
    across(0.001, 1000, 100000, [&](f64 x) {
        const auto f = static_cast<f32>(x);
        log_most = std::max(log_most, floats_apart(math::log(f), std::log(f)));
        for (const f32 y : { -3.5f, -1.0f, 0.5f, 2.0f, 2.7f })
            if (std::isfinite(std::pow(f, y)))
                pow_most = std::max(pow_most, floats_apart(math::pow(f, y), std::pow(f, y)));
    });
    CHECK(exp_most <= 1);
    CHECK(log_most <= 1);
    CHECK(pow_most <= 1);
    CHECK(math::exp(0.0) == 1);
    CHECK(math::log(1.0) == 0);
    CHECK(math::pow(-2.0, 3.0) == doctest::Approx(-8.0));
    CHECK(std::isnan(math::pow(-2.0, 0.5)));
}

TEST_CASE("the portable maths gives the same bits on every machine")
{
    // Worked out once by these functions. A build or platform that gives other bits would play a shared park
    // out differently, so this fails there.
    // Each literal is the shortest decimal that reads back as exactly that double.
    CHECK(math::sin(1.0) == 0.8414709848078965);
    CHECK(math::cos(1.0) == 0.5403023058681398);
    CHECK(math::atan2(1.0, 2.0) == 0.46364760900080604);
    CHECK(math::exp(1.0) == 2.7182818284590455);
    CHECK(math::log(10.0) == 2.302585092994046);
    CHECK(math::pow(2.5, 3.7) == 29.67413253642087);
}

TEST_CASE("a portable turn about an axis is glm's")
{
    const glm::vec3 axis = glm::normalize(glm::vec3 { 0.3f, -1.0f, 0.5f });
    for (const f32 angle : { -2.0f, 0.0f, 0.7f, 3.0f })
    {
        const glm::mat4 ours = math::rotation(angle, axis * 2.0f);
        const glm::mat4 theirs = glm::rotate(glm::mat4(1.0f), angle, axis * 2.0f);
        for (i32 c = 0; c < 4; ++c)
            for (i32 r = 0; r < 4; ++r)
                CHECK(ours[c][r] == doctest::Approx(theirs[c][r]).epsilon(1e-6));
        const glm::quat q = math::angle_axis(angle, axis);
        const glm::quat g = glm::angleAxis(angle, axis);
        CHECK(q.w == doctest::Approx(g.w).epsilon(1e-6));
        CHECK(q.x == doctest::Approx(g.x).epsilon(1e-6));
        CHECK(q.y == doctest::Approx(g.y).epsilon(1e-6));
        CHECK(q.z == doctest::Approx(g.z).epsilon(1e-6));
    }
}
