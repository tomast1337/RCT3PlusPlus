// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/staff_care.hpp"

#include <algorithm>
#include <cstddef>

namespace rct3::simulation
{

    namespace
    {
        // First guesses to tune by watching parks run.
        constexpr std::int64_t kLowestWage = 50;     ///< Hundredths an hour at level 0; each level adds half as much
        constexpr float kLoadSeconds = 600;          ///< Over how long their share of time at work is judged
        constexpr float kContentBase = 0.45f;        ///< Happiness of an untrained worker on the lowest wage
        constexpr float kContentPerTraining = 0.1f;  ///< More for each level of training
        constexpr float kContentPerWage = 0.1f;      ///< More for each wage level
        constexpr float kOverworked = 0.85f;         ///< Share of time at work past which they feel overworked
        constexpr float kBored = 0.15f;              ///< Share below which they feel idle
        constexpr float kOverworkGloom = 2;          ///< Happiness lost for each share past overworked
        constexpr float kBoredGloom = 3;             ///< And for each share below idle
        constexpr float kMoodPerSecond = 1.0f / 900; ///< How fast happiness goes to where it is heading
        constexpr float kLazyBelow = 0.5f;           ///< Happiness below which laziness grows
        constexpr float kLazyPerSecond = 1.0f / 600; ///< Laziness grown a second for each share below that, untrained
        constexpr float kLazyFadePerSecond = 1.0f / 3600;
        constexpr float kLazyAfterWarning = 0.3f;   ///< Laziness at which a warning is deserved
        constexpr float kWarningGloom = 0.2f;       ///< Happiness a warning not deserved takes away
        constexpr float kRestPerSecond = 1.0f / 60; ///< Chance to rest a second, for a worker wholly lazy
    } // namespace

    std::int64_t wage_at(std::int32_t level)
    {
        return kLowestWage + kLowestWage / 2 * std::clamp(level, 0, kMostWageLevel);
    }

    void set_wage_level(Staff& staff, std::int32_t level)
    {
        staff.wage_level = std::clamp(level, 0, kMostWageLevel);
        staff.wage = wage_at(staff.wage_level);
    }

    std::int64_t training_cost(const Staff& staff)
    {
        return staff.training >= 0 && staff.training < kMostTraining ? kTrainingCost[static_cast<std::size_t>(staff.training)] : 0;
    }

    bool train(Staff& staff)
    {
        if (staff.training >= kMostTraining)
            return false;
        staff.training = std::max(staff.training, 0) + 1;
        return true;
    }

    void warn(Staff& staff)
    {
        ++staff.warnings;
        if (staff.laziness >= kLazyAfterWarning)
            staff.laziness = 0;
        else
            staff.happiness = std::max(staff.happiness - kWarningGloom, 0.0f);
    }

    void care_for(Staff& staff, bool working, float seconds)
    {
        staff.load += ((working ? 1.0f : 0.0f) - staff.load) * std::min(seconds / kLoadSeconds, 1.0f);
        const float overwork = std::max(staff.load - kOverworked, 0.0f) * kOverworkGloom;
        const float boredom = std::max(kBored - staff.load, 0.0f) * kBoredGloom;
        const float content = std::clamp(kContentBase + kContentPerTraining * static_cast<float>(staff.training) + kContentPerWage * static_cast<float>(staff.wage_level) - overwork - boredom, 0.0f, 1.0f);
        const float step = kMoodPerSecond * seconds;
        staff.happiness = staff.happiness < content ? std::min(staff.happiness + step, content) : std::max(staff.happiness - step, content);
        if (staff.happiness < kLazyBelow)
            staff.laziness += (kLazyBelow - staff.happiness) * kLazyPerSecond / static_cast<float>(1 + std::max(staff.training, 0)) * seconds;
        else
            staff.laziness -= kLazyFadePerSecond * seconds;
        staff.laziness = std::clamp(staff.laziness, 0.0f, 1.0f);
    }

    float work_slowdown(const Staff& staff)
    {
        return 1 + staff.laziness;
    }

    float rest_chance(const Staff& staff)
    {
        return staff.laziness * kRestPerSecond;
    }

} // namespace rct3::simulation
