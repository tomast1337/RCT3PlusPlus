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
        return kTuning.staff.lowest_wage.value + kTuning.staff.lowest_wage.value / 2 * std::clamp(level, 0, kMostWageLevel);
    }

    void set_wage_level(Staff& staff, std::int32_t level)
    {
        staff.wage_level = std::clamp(level, 0, kMostWageLevel);
        staff.wage.value = wage_at(staff.wage_level);
    }

    std::int64_t training_cost(const Staff& staff)
    {
        return staff.training >= 0 && staff.training < kMostTraining ? kTuning.staff.training_cost[static_cast<std::size_t>(staff.training)].value : 0;
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
        if (staff.laziness.value >= kTuning.staff.lazy_after_warning.value)
            staff.laziness.value = 0;
        else
            staff.happiness.value = std::max(staff.happiness.value - kTuning.staff.warning_gloom.value, 0.0f);
    }

    void care_for(Staff& staff, bool working, float seconds)
    {
        staff.load.value += ((working ? 1.0f : 0.0f) - staff.load.value) * std::min(seconds / kTuning.staff.load_window.value, 1.0f);
        const float overwork = std::max(staff.load.value - kTuning.staff.overworked.value, 0.0f) * kTuning.staff.overwork_gloom;
        const float boredom = std::max(kTuning.staff.bored.value - staff.load.value, 0.0f) * kTuning.staff.bored_gloom;
        const float content = std::clamp(kTuning.staff.content_base.value + kTuning.staff.content_per_training.value * static_cast<float>(staff.training) + kTuning.staff.content_per_wage.value * static_cast<float>(staff.wage_level) - overwork - boredom, 0.0f, 1.0f);
        const float step = kTuning.staff.mood.value * seconds;
        staff.happiness.value = staff.happiness.value < content ? std::min(staff.happiness.value + step, content) : std::max(staff.happiness.value - step, content);
        if (staff.happiness.value < kTuning.staff.lazy_below.value)
            staff.laziness.value += (kTuning.staff.lazy_below.value - staff.happiness.value) * kTuning.staff.lazy.value / static_cast<float>(1 + std::max(staff.training, 0)) * seconds;
        else
            staff.laziness.value -= kTuning.staff.lazy_fade.value * seconds;
        staff.laziness.value = std::clamp(staff.laziness.value, 0.0f, 1.0f);
    }

    float work_slowdown(const Staff& staff)
    {
        return 1 + staff.laziness.value;
    }

    float rest_chance(const Staff& staff)
    {
        return staff.laziness.value * kTuning.staff.rest.value;
    }

} // namespace rct3::simulation
