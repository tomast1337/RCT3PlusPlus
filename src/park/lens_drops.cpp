// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/lens_drops.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace rct3::park
{

    namespace
    {
        // Chosen by eye.
        constexpr float kSmallest = 0.004f; ///< Radius of the smallest drop
        constexpr float kLargest = 0.018f;  ///< Of the largest that lands
        constexpr float kSheetSmallest = 0.18f;
        constexpr float kSheetLargest = 0.32f;
        constexpr float kHeavy = 0.012f;   ///< A drop this big slides at the full pull of gravity
        constexpr float kGravity = 0.9f;   ///< Its slide down the lens
        constexpr float kHold = 0.55f;     ///< The push a drop holds still against: a small one holds against gravity
        constexpr float kSlide = 0.3f;     ///< Its speed down the lens per push
        constexpr float kOnrush = 0.02f;   ///< Push out from the middle per world unit a second forward
        constexpr float kSettle = 6.0f;    ///< How quickly a drop's speed follows its push, a second
        constexpr float kWobble = 0.25f;   ///< Its slide's sideways wander, a share of its speed
        constexpr float kSheetFall = 0.6f; ///< How fast a sheet runs down
        constexpr float kSheetLife = 1.4f; ///< Seconds a sheet lasts
        constexpr float kDropLife = 9.0f;  ///< Seconds a drop lasts at most; most dry sooner
        constexpr float kMeet = 0.8f;      ///< Two drops run together when closer than this share of their radii

        float between(random::RandomState& dice, float low, float high)
        {
            return low + (high - low) * static_cast<float>(random::unit(dice));
        }
    } // namespace

    void wet_lens(LensWater& water, std::size_t drops, std::size_t sheets, float aspect)
    {
        auto& d = water.dice;
        const auto count = [&](bool sheet) { return static_cast<std::size_t>(std::ranges::count(water.drops, sheet, &LensDrop::sheet)); };
        for (std::size_t i = count(true); i < kMostLensSheets && sheets > 0; ++i, --sheets)
        {
            const float radius = between(d, kSheetSmallest, kSheetLargest);
            water.drops.push_back({ .at = { between(d, 0.0f, aspect), between(d, -0.2f, 0.6f) }, .velocity = { 0, kSheetFall * 0.5f }, .radius = radius, .life = kSheetLife * between(d, 0.7f, 1.0f), .sheet = true });
        }
        for (std::size_t i = count(false); i < kMostLensDrops && drops > 0; ++i, --drops)
        {
            // Most drops are small: the square of an even share leans towards the small end.
            const float share = static_cast<float>(random::unit(d));
            const float radius = kSmallest + (kLargest - kSmallest) * share * share;
            water.drops.push_back({ .at = { between(d, 0.0f, aspect), between(d, 0.0f, 1.0f) }, .radius = radius, .clinging = between(d, 0.1f, 1.5f), .life = kDropLife * between(d, 0.4f, 1.0f) });
        }
    }

    void run_lens_water(LensWater& water, const LensPush& push, float seconds)
    {
        auto& d = water.dice;
        const glm::vec2 middle { push.aspect * 0.5f, 0.5f };
        const float settling = std::min(1.0f, seconds * kSettle);
        for (LensDrop& drop : water.drops)
        {
            drop.age += seconds;
            if (drop.sheet)
            {
                drop.velocity.y += kSheetFall * seconds;
                drop.at += (drop.velocity + push.turn * 0.5f) * seconds;
                continue;
            }
            const glm::vec2 out = drop.at - middle;
            const glm::vec2 shove = push.down * kGravity * (drop.radius / kHeavy) + push.wind + push.turn + out * (push.onrush * kOnrush);
            if (drop.clinging > 0)
                drop.clinging -= seconds;
            const bool held = drop.clinging > 0 || glm::length(shove) < kHold;
            const glm::vec2 wanted = held ? glm::vec2 { } : shove * kSlide;
            drop.velocity += (wanted - drop.velocity) * settling;
            // A sliding drop wanders a little from side to side, and now and then catches and holds again.
            if (const float speed = glm::length(drop.velocity); speed > 0 && !held)
            {
                const glm::vec2 side { -drop.velocity.y / speed, drop.velocity.x / speed };
                drop.velocity += side * (speed * kWobble * between(d, -1.0f, 1.0f));
                if (random::unit(d) < seconds * 0.5)
                    drop.clinging = between(d, 0.05f, 0.4f);
            }
            drop.at += drop.velocity * seconds;
        }
        // Drops that touch run together into the bigger, keeping their water.
        for (std::size_t i = 0; i < water.drops.size(); ++i)
        {
            LensDrop& a = water.drops[i];
            if (a.sheet || a.radius <= 0)
                continue;
            for (std::size_t j = i + 1; j < water.drops.size(); ++j)
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
            const float r = drop.radius;
            return r <= 0 || drop.age >= drop.life || drop.at.y - r > 1 || drop.at.y + r < -0.5f || drop.at.x + r < 0 || drop.at.x - r > push.aspect;
        });
    }

} // namespace rct3::park
