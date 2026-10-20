// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/staff_care.hpp"

#include "simulation/tuning.hpp"

#include <algorithm>
#include <cstddef>

namespace rct3::simulation
{

    static_assert(kTuning.staff.training_cost.size() == kMostTraining);

    std::int64_t wage_at(std::int32_t level)
    {
        return kTuning.staff.lowest_wage + kTuning.staff.lowest_wage / 2 * std::clamp(level, 0, kMostWageLevel);
    }

    void set_wage_level(Staff& staff, std::int32_t level)
    {
        staff.wage_level = std::clamp(level, 0, kMostWageLevel);
        staff.wage = wage_at(staff.wage_level);
    }

    std::int64_t training_cost(const Staff& staff)
    {
        return staff.training >= 0 && staff.training < kMostTraining ? kTuning.staff.training_cost[static_cast<std::size_t>(staff.training)] : 0;
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
        if (staff.laziness >= kTuning.staff.lazy_after_warning)
            staff.laziness = 0;
        else
            staff.happiness = std::max(staff.happiness - kTuning.staff.warning_gloom, 0.0f);
    }

    void care_for(Staff& staff, bool working, float seconds)
    {
        staff.load += ((working ? 1.0f : 0.0f) - staff.load) * std::min(seconds / kTuning.staff.load_seconds, 1.0f);
        const float overwork = std::max(staff.load - kTuning.staff.overworked, 0.0f) * kTuning.staff.overwork_gloom;
        const float boredom = std::max(kTuning.staff.bored - staff.load, 0.0f) * kTuning.staff.bored_gloom;
        const float content = std::clamp(kTuning.staff.content_base + kTuning.staff.content_per_training * static_cast<float>(staff.training) + kTuning.staff.content_per_wage * static_cast<float>(staff.wage_level) - overwork - boredom, 0.0f, 1.0f);
        const float step = kTuning.staff.mood_per_second * seconds;
        staff.happiness = staff.happiness < content ? std::min(staff.happiness + step, content) : std::max(staff.happiness - step, content);
        if (staff.happiness < kTuning.staff.lazy_below)
            staff.laziness += (kTuning.staff.lazy_below - staff.happiness) * kTuning.staff.lazy_per_second / static_cast<float>(1 + std::max(staff.training, 0)) * seconds;
        else
            staff.laziness -= kTuning.staff.lazy_fade_per_second * seconds;
        staff.laziness = std::clamp(staff.laziness, 0.0f, 1.0f);
    }

    float work_slowdown(const Staff& staff)
    {
        return 1 + staff.laziness;
    }

    float rest_chance(const Staff& staff)
    {
        return staff.laziness * kTuning.staff.rest_per_second;
    }

} // namespace rct3::simulation
