// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/lens_drops.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace rct3::park
{

    namespace
    {
        // Chosen by eye.
        constexpr f32 kSmallest = 0.004f; ///< Radius of the smallest drop
        constexpr f32 kLargest = 0.018f;  ///< Of the largest that lands
        constexpr f32 kSheetSmallest = 0.18f;
        constexpr f32 kSheetLargest = 0.32f;
        constexpr f32 kHeavy = 0.012f;                  ///< A drop this big slides at the full pull of gravity
        constexpr f32 kGravity = 0.9f;                  ///< Its slide down the lens
        constexpr f32 kHold = 0.55f;                    ///< The push a drop holds still against: a small one holds against gravity
        constexpr f32 kSlide = 0.3f;                    ///< Its speed down the lens per push
        constexpr f32 kOnrush = 0.02f;                  ///< Push out from the middle per world unit a second forward
        constexpr units::PerSecond kSettle { 6.0f };    ///< How quickly a drop's speed follows its push
        constexpr f32 kWobble = 0.25f;                  ///< Its slide's sideways wander, a share of its speed
        constexpr f32 kSheetFall = 0.6f;                ///< How fast a sheet runs down
        constexpr units::Seconds kSheetLife { 1.4f };   ///< A sheet lasts
        constexpr units::Seconds kDropLife { 9.0f };    ///< A drop lasts at most; most dry sooner
        constexpr f32 kMeet = 0.8f;                     ///< Two drops run together when closer than this share of their radii
        constexpr f32 kSheetShortest = 0.7f;            ///< Of kSheetLife, the least a sheet lasts
        constexpr f32 kDropShortest = 0.4f;             ///< Of kDropLife, the least a drop lasts
        constexpr units::Seconds kClingLeast { 0.1f };  ///< A drop that lands holds still this long, at least
        constexpr units::Seconds kClingMost { 1.5f };   ///< And at most
        constexpr units::Seconds kCatchLeast { 0.05f }; ///< A sliding drop that catches holds still this long, at least
        constexpr units::Seconds kCatchMost { 0.4f };   ///< And at most

        f32 between(random::RandomState& dice, f32 low, f32 high)
        {
            return low + (high - low) * static_cast<f32>(random::unit(dice));
        }
    } // namespace

    void wet_lens(LensWater& water, usize drops, usize sheets, f32 aspect)
    {
        auto& d = water.dice;
        const auto count = [&](bool sheet) { return static_cast<usize>(std::ranges::count(water.drops, sheet, &LensDrop::sheet)); };
        for (usize i = count(true); i < kMostLensSheets && sheets > 0; ++i, --sheets)
        {
            const f32 radius = between(d, kSheetSmallest, kSheetLargest);
            water.drops.push_back({ .at = { between(d, 0.0f, aspect), between(d, -0.2f, 0.6f) }, .velocity = { 0, kSheetFall * 0.5f }, .radius = radius, .life = { kSheetLife.value * between(d, kSheetShortest, 1.0f) }, .sheet = true });
        }
        for (usize i = count(false); i < kMostLensDrops && drops > 0; ++i, --drops)
        {
            // Most drops are small: the square of an even share leans towards the small end.
            const f32 share = static_cast<f32>(random::unit(d));
            const f32 radius = kSmallest + (kLargest - kSmallest) * share * share;
            water.drops.push_back({ .at = { between(d, 0.0f, aspect), between(d, 0.0f, 1.0f) }, .radius = radius, .clinging = { between(d, kClingLeast.value, kClingMost.value) }, .life = { kDropLife.value * between(d, kDropShortest, 1.0f) } });
        }
    }

    void run_lens_water(LensWater& water, const LensPush& push, f32 seconds)
    {
        auto& d = water.dice;
        const glm::vec2 middle { push.aspect * 0.5f, 0.5f };
        const f32 settling = std::min(1.0f, seconds * kSettle.value);
        for (LensDrop& drop : water.drops)
        {
            drop.age.value += seconds;
            if (drop.sheet)
            {
                drop.velocity.y += kSheetFall * seconds;
                drop.at += (drop.velocity + push.turn * 0.5f) * seconds;
                continue;
            }
            const glm::vec2 out = drop.at - middle;
            const glm::vec2 shove = push.down * kGravity * (drop.radius / kHeavy) + push.wind + push.turn + out * (push.onrush.value * kOnrush);
            if (drop.clinging.value > 0)
                drop.clinging.value -= seconds;
            const bool held = drop.clinging.value > 0 || glm::length(shove) < kHold;
            const glm::vec2 wanted = held ? glm::vec2 { } : shove * kSlide;
            drop.velocity += (wanted - drop.velocity) * settling;
            // A sliding drop wanders a little from side to side, and now and then catches and holds again.
            if (const f32 speed = glm::length(drop.velocity); speed > 0 && !held)
            {
                const glm::vec2 side { -drop.velocity.y / speed, drop.velocity.x / speed };
                drop.velocity += side * (speed * kWobble * between(d, -1.0f, 1.0f));
                if (random::unit(d) < seconds * 0.5)
                    drop.clinging = { between(d, kCatchLeast.value, kCatchMost.value) };
            }
            drop.at += drop.velocity * seconds;
        }
        // Drops that touch run together into the bigger, keeping their water.
        for (usize i = 0; i < water.drops.size(); ++i)
        {
            LensDrop& a = water.drops[i];
            if (a.sheet || a.radius <= 0)
                continue;
            for (usize j = i + 1; j < water.drops.size(); ++j)
            {
                LensDrop& b = water.drops[j];
                if (b.sheet || b.radius <= 0 || glm::distance(a.at, b.at) >= (a.radius + b.radius) * kMeet)
                    continue;
                LensDrop& big = a.radius >= b.radius ? a : b;
                LensDrop& small = a.radius >= b.radius ? b : a;
                big.radius = std::sqrt(big.radius * big.radius + small.radius * small.radius);
                big.life = std::max(big.life - big.age, small.life - small.age) + big.age;
                small.radius = 0;
                if (&small == &a)
                    break;
            }
        }
        std::erase_if(water.drops, [&](const LensDrop& drop) {
            const f32 r = drop.radius;
            return r <= 0 || drop.age >= drop.life || drop.at.y - r > 1 || drop.at.y + r < -0.5f || drop.at.x + r < 0 || drop.at.x - r > push.aspect;
        });
    }

} // namespace rct3::park
