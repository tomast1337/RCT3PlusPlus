// SPDX-License-Identifier: GPL-3.0-or-later
#include "math/portable.hpp"

#include "core/types.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>

namespace rct3::math
{

    namespace
    {
        // pi/2 as a sum of three doubles: pi/2 rounded to 33 significant bits, what is left rounded to 33 bits, and
        // the rest to 53. With the first two short, k times each is exact for any |k| below 2^20, so x - k pi/2
        // loses nothing to rounding (Cody and Waite's reduction). Worked out from 80 digits of pi.
        constexpr f64 kHalfPiHigh = 0x1.921fb544p+0;
        constexpr f64 kHalfPiMid = 0x1.0b4611a6p-34;
        constexpr f64 kHalfPiLow = 0x1.3198a2e037073p-69;
        // ln 2 the same way, in two parts: exp's k stays below 2^11.
        constexpr f64 kLn2High = 0x1.62e42ffp-1;
        constexpr f64 kLn2Low = -0x1.718432a1b0e26p-35;

        /// The polynomial with coefficients `c`, highest power first, at `x`: Horner's rule run as two chains,
        /// one over the even and one over the odd powers, in x^2, so the two run side by side and the result
        /// comes in about half the time of one chain. The order of operations is fixed, so the bits are too.
        template<usize N> f64 polynomial(const std::array<f64, N>& c, f64 x)
        {
            const f64 x2 = x * x;
            // c[N - 1] is the constant term; even powers sit at N - 1, N - 3, ...
            f64 even = 0;
            f64 odd = 0;
            for (usize i = 0; i < N; ++i)
            {
                const usize power = N - 1 - i;
                if (power % 2 == 0)
                    even = even * x2 + c[i];
                else
                    odd = odd * x2 + c[i];
            }
            return even + x * odd;
        }

        /// 1 / n! for n = 0..20, worked out once by repeated division.
        constexpr std::array<f64, 21> kInverseFactorial = [] {
            std::array<f64, 21> out { };
            f64 f = 1;
            for (usize n = 0; n < out.size(); ++n)
            {
                if (n > 0)
                    f *= static_cast<f64>(n);
                out[n] = 1 / f;
            }
            return out;
        }();

        /// sin r for |r| <= pi/4: the Taylor series to r^17, past which the terms are below 1e-19.
        f64 sin_near(f64 r)
        {
            const f64 r2 = r * r;
            const std::array<f64, 8> c { kInverseFactorial[17], -kInverseFactorial[15], kInverseFactorial[13], -kInverseFactorial[11], kInverseFactorial[9], -kInverseFactorial[7], kInverseFactorial[5], -kInverseFactorial[3] };
            return r + r * r2 * polynomial(c, r2);
        }

        /// cos r for |r| <= pi/4: the Taylor series to r^18.
        f64 cos_near(f64 r)
        {
            const f64 r2 = r * r;
            const std::array<f64, 9> c { -kInverseFactorial[18], kInverseFactorial[16], -kInverseFactorial[14], kInverseFactorial[12], -kInverseFactorial[10], kInverseFactorial[8], -kInverseFactorial[6], kInverseFactorial[4], -kInverseFactorial[2] };
            return 1 + r2 * polynomial(c, r2);
        }

        /// 1 / (2n + 1) for n = 0..14, the odd series' coefficients.
        constexpr std::array<f64, 15> kInverseOdd = [] {
            std::array<f64, 15> out { };
            for (usize n = 0; n < out.size(); ++n)
                out[n] = 1 / static_cast<f64>(2 * n + 1);
            return out;
        }();

        /// The alternating odd series' coefficients, highest power first: -1/27, 1/25, ..., -1/3.
        constexpr std::array<f64, 13> kAtanSeries = [] {
            std::array<f64, 13> out { };
            for (usize i = 0; i < out.size(); ++i)
            {
                const usize n = out.size() - i; // The power is 2n + 1
                out[i] = n % 2 == 0 ? kInverseOdd[n] : -kInverseOdd[n];
            }
            return out;
        }();

        /// atan t for |t| <= tan(pi/12): the series t - t^3/3 + t^5/5 ... to t^27, past which the terms are below
        /// 1e-17.
        f64 atan_near(f64 t)
        {
            const f64 t2 = t * t;
            return t + t * t2 * polynomial(kAtanSeries, t2);
        }

        /// The whole number nearest `x`, ties to even, for |x| below 2^51: adding and taking away 1.5 * 2^52
        /// leaves no bits below the point, by plain double arithmetic, with no library call.
        f64 nearest_whole(f64 x)
        {
            constexpr f64 kShift = 0x1.8p52;
            return (x + kShift) - kShift;
        }

        /// 1/n! from n = 13 down to 0: exp's Taylor series to r^13, past which the terms are below 1e-17 for
        /// |r| <= ln2/2.
        constexpr std::array<f64, 14> kExpSeries = [] {
            std::array<f64, 14> out { };
            for (usize i = 0; i < out.size(); ++i)
                out[i] = kInverseFactorial[out.size() - 1 - i];
            return out;
        }();

        /// The quarter turns `x` is from 0 and what is left over, between -pi/4 and pi/4.
        struct Quarter
        {
            i64 turns { };
            f64 rest { };
        };
        Quarter quarter(f64 x)
        {
            const f64 k = nearest_whole(x * (2 / std::numbers::pi));
            return { .turns = static_cast<i64>(k), .rest = ((x - k * kHalfPiHigh) - k * kHalfPiMid) - k * kHalfPiLow };
        }
    } // namespace

    f64 sin(f64 x)
    {
        if (!std::isfinite(x))
            return std::numeric_limits<f64>::quiet_NaN();
        const Quarter q = quarter(x);
        switch (q.turns & 3)
        {
            case 0:
                return sin_near(q.rest);
            case 1:
                return cos_near(q.rest);
            case 2:
                return -sin_near(q.rest);
            default:
                return -cos_near(q.rest);
        }
    }

    f64 cos(f64 x)
    {
        if (!std::isfinite(x))
            return std::numeric_limits<f64>::quiet_NaN();
        const Quarter q = quarter(x);
        switch (q.turns & 3)
        {
            case 0:
                return cos_near(q.rest);
            case 1:
                return -sin_near(q.rest);
            case 2:
                return -cos_near(q.rest);
            default:
                return sin_near(q.rest);
        }
    }

    f64 atan(f64 t)
    {
        if (std::isnan(t))
            return t;
        const bool negative = t < 0;
        f64 a = std::abs(t);
        // Past 1, from the other side of the right angle; past tan(pi/12), as pi/6 and what is left.
        const bool flipped = a > 1;
        if (flipped)
            a = 1 / a;
        constexpr f64 kTanTwelfth = 2 - std::numbers::sqrt3;
        const bool shifted = a > kTanTwelfth;
        if (shifted)
            a = (a * std::numbers::sqrt3 - 1) / (std::numbers::sqrt3 + a);
        f64 out = atan_near(a);
        if (shifted)
            out += std::numbers::pi / 6;
        if (flipped)
            out = std::numbers::pi / 2 - out;
        return negative ? -out : out;
    }

    f64 atan2(f64 y, f64 x)
    {
        if (std::isnan(x) || std::isnan(y))
            return std::numeric_limits<f64>::quiet_NaN();
        // Zeros and infinities as IEEE 754 gives them, by the signs of both.
        if (y == 0)
            return std::signbit(x) ? std::copysign(std::numbers::pi, y) : y;
        if (x == 0)
            return std::copysign(std::numbers::pi / 2, y);
        if (std::isinf(x) || std::isinf(y))
        {
            const f64 across = std::isinf(x) ? (std::isinf(y) ? std::numbers::pi / 4 : 0) : std::numbers::pi / 2;
            const f64 angle = std::isinf(x) && x < 0 ? std::numbers::pi - across : across;
            return std::copysign(angle, y);
        }
        // Steep: a right angle less the angle off the vertical. Else the angle off the horizontal, turned half
        // round when x points back.
        if (std::abs(y) > std::abs(x))
            return std::copysign(std::numbers::pi / 2, y) - atan(x / y);
        const f64 off = atan(y / x);
        return x > 0 ? off : off + std::copysign(std::numbers::pi, y);
    }

    f64 exp(f64 x)
    {
        if (std::isnan(x))
            return x;
        if (x > 709.78)
            return std::numeric_limits<f64>::infinity();
        if (x < -745.2)
            return 0;
        const f64 k = nearest_whole(x / std::numbers::ln2);
        const f64 r = (x - k * kLn2High) - k * kLn2Low; // |r| <= ln2/2
        const f64 near = polynomial(kExpSeries, r);
        // Times 2^k, built from its bits; below the smallest normal number in two steps.
        const auto power_of_two = [](i64 e) { return std::bit_cast<f64>(static_cast<u64>(e + 1023) << 52); };
        const auto whole = static_cast<i64>(k);
        return whole >= -1022 ? near * power_of_two(whole) : near * power_of_two(whole + 600) * power_of_two(-600);
    }

    f64 log(f64 x)
    {
        if (std::isnan(x) || x < 0)
            return std::numeric_limits<f64>::quiet_NaN();
        if (x == 0)
            return -std::numeric_limits<f64>::infinity();
        if (std::isinf(x))
            return x;
        i32 e = 0;
        f64 m = std::frexp(x, &e); // x = m 2^e, m in [0.5, 1)
        if (m < std::numbers::sqrt2 / 2)
        {
            m *= 2;
            --e;
        }
        // log m = 2 atanh s with s = (m - 1) / (m + 1), |s| <= 0.172: the series to s^29.
        const f64 s = (m - 1) / (m + 1);
        const f64 s2 = s * s;
        f64 sum = 0;
        for (usize n = 14; n >= 1; --n)
            sum = sum * s2 + kInverseOdd[n];
        const f64 log_m = 2 * (s + s * s2 * sum);
        return static_cast<f64>(e) * kLn2High + (static_cast<f64>(e) * kLn2Low + log_m);
    }

    f64 pow(f64 x, f64 y)
    {
        if (y == 0)
            return 1;
        if (x == 1)
            return 1;
        if (std::isnan(x) || std::isnan(y))
            return std::numeric_limits<f64>::quiet_NaN();
        const bool whole = std::trunc(y) == y;
        if (x == 0)
            return y > 0 ? 0 : std::numeric_limits<f64>::infinity();
        if (x < 0)
        {
            if (!whole)
                return std::numeric_limits<f64>::quiet_NaN();
            const bool odd = std::fmod(y, 2) != 0;
            const f64 out = exp(y * log(-x));
            return odd ? -out : out;
        }
        return exp(y * log(x));
    }

    f32 sin(f32 x)
    {
        return static_cast<f32>(sin(f64 { x }));
    }

    f32 cos(f32 x)
    {
        return static_cast<f32>(cos(f64 { x }));
    }

    f32 atan2(f32 y, f32 x)
    {
        return static_cast<f32>(atan2(f64 { y }, f64 { x }));
    }

    f32 exp(f32 x)
    {
        return static_cast<f32>(exp(f64 { x }));
    }

    f32 log(f32 x)
    {
        return static_cast<f32>(log(f64 { x }));
    }

    f32 pow(f32 x, f32 y)
    {
        return static_cast<f32>(pow(f64 { x }, f64 { y }));
    }

    glm::mat4 rotation(f32 angle, glm::vec3 axis)
    {
        // Rodrigues' formula, laid out as glm::rotate lays it out.
        const f32 c = cos(angle);
        const f32 s = sin(angle);
        const glm::vec3 a = glm::normalize(axis);
        const glm::vec3 t = (1 - c) * a;
        glm::mat4 m(1.0f);
        m[0] = { c + t.x * a.x, t.x * a.y + s * a.z, t.x * a.z - s * a.y, 0 };
        m[1] = { t.y * a.x - s * a.z, c + t.y * a.y, t.y * a.z + s * a.x, 0 };
        m[2] = { t.z * a.x + s * a.y, t.z * a.y - s * a.x, c + t.z * a.z, 0 };
        return m;
    }

    glm::quat angle_axis(f32 angle, glm::vec3 axis)
    {
        const f32 half = angle / 2;
        const f32 s = sin(half);
        return { cos(half), axis.x * s, axis.y * s, axis.z * s };
    }

} // namespace rct3::math
