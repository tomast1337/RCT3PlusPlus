// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/staff_care.hpp"

#include "simulation/tuning.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <variant>

namespace rct3::simulation
{

    static_assert(kTuning.staff.training_cost.size() == kMostTraining);

    StaffRole staff_role(save::StaffKind kind)
    {
        using enum save::StaffKind;
        return kind == Handyman ? StaffRole::Handyman : kind == Mechanic ? StaffRole::Mechanic : kind == SecurityGuard ? StaffRole::Guard : kind == Vendor ? StaffRole::Vendor : kind == AnimalKeeper ? StaffRole::Keeper : kind == ParkInspector ? StaffRole::Inspector : kind == Entertainer ? StaffRole::Entertainer : StaffRole::Other;
    }

    bool walks_paths(StaffRole role)
    {
        return role != StaffRole::Vendor && role != StaffRole::Keeper && role != StaffRole::Other;
    }

    std::int64_t wage_at(StaffRole role, std::int32_t level)
    {
        return kTuning.staff.wages.at(std::to_underlying(role))[static_cast<std::size_t>(std::clamp(level, 0, kMostWageLevel))];
    }

    void set_wage_level(Staff& staff, std::int32_t level)
    {
        staff.wage_level = std::clamp(level, 0, kMostWageLevel);
        staff.wage.value = wage_at(staff.role, staff.wage_level);
    }

    std::int64_t training_cost(const Staff& staff)
    {
        return staff.training >= 0 && staff.training < kMostTraining ? kTuning.staff.training_cost[static_cast<std::size_t>(staff.training)].value : 0;
    }

    std::int32_t hired_training(bool park_has_money)
    {
        return park_has_money ? 0 : kMostTraining;
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
        if (staff.role == StaffRole::Inspector) // An inspector's mood and laziness never change
            return;
        staff.load.value += ((working ? 1.0f : 0.0f) - staff.load.value) * std::min(seconds / kTuning.staff.load_window.value, 1.0f);
        // An entertainer's work comes in short shows, so their share of time at work is no measure of them.
        const bool entertainer = staff.role == StaffRole::Entertainer;
        const float overwork = entertainer ? 0.0f : std::max(staff.load.value - kTuning.staff.overworked.value, 0.0f) * kTuning.staff.overwork_gloom;
        const float boredom = entertainer ? 0.0f : std::max(kTuning.staff.bored.value - staff.load.value, 0.0f) * kTuning.staff.bored_gloom;
        const float content = std::clamp(kTuning.staff.content_base.value + kTuning.staff.content_per_training.value * static_cast<float>(staff.training) + kTuning.staff.content_per_wage.value * static_cast<float>(staff.wage_level) - overwork - boredom, 0.0f, 1.0f);
        const float step = kTuning.staff.mood.value * seconds;
        if (entertainer && !working)
            staff.happiness.value = std::max(staff.happiness.value - kTuning.staff.lonely_gloom.value * seconds / static_cast<float>(1 + std::max(staff.training, 0)), 0.0f);
        else
            staff.happiness.value = staff.happiness.value < content ? std::min(staff.happiness.value + step, content) : std::max(staff.happiness.value - step, content);
        if (staff.happiness.value < kTuning.staff.lazy_below.value)
            staff.laziness.value += (kTuning.staff.lazy_below.value - staff.happiness.value) * kTuning.staff.lazy.value / static_cast<float>(1 + std::max(staff.training, 0)) * seconds;
        else
            staff.laziness.value -= kTuning.staff.lazy_fade.value * seconds;
        staff.laziness.value = std::clamp(staff.laziness.value, 0.0f, 1.0f);
    }

    void take_saved_work(Staff& staff, const save::Staff& saved)
    {
        if (const auto* w = std::get_if<save::HandymanWork>(&saved.work))
        {
            staff.swept = w->paths_swept;
            staff.emptied = w->bins_emptied;
            staff.watered = w->gardens_watered;
        }
        else if (const auto* m = std::get_if<save::MechanicWork>(&saved.work))
        {
            staff.fixed = m->objects_fixed;
            staff.inspected = m->objects_inspected;
        }
        else if (const auto* k = std::get_if<save::KeeperWork>(&saved.work))
        {
            staff.cleared = k->dung_cleared;
            staff.filled = k->troughs_filled;
        }
    }

    std::vector<ThoughtKind> staff_thoughts(Staff& staff)
    {
        const auto& t = kTuning.staff;
        std::vector<ThoughtKind> out;
        if (staff.role == StaffRole::Inspector) // Their thoughts are their reports, made on their rounds
            return out;
        if (staff.load.value < t.bored.value)
            out.push_back(ThoughtKind::StaffBored);
        else if (staff.load.value > t.overworked.value)
            out.push_back(staff.wage_level == 0 ? ThoughtKind::StaffUnderpaid : ThoughtKind::StaffTooBusy);
        staff.miserable = staff.happiness.value < t.miserable.value ? staff.miserable + 1 : 0;
        if (staff.miserable == t.quit_after + 1) // Once a miserable stretch
            out.push_back(ThoughtKind::StaffWantToQuit);
        else if (staff.miserable > 0)
            out.push_back(ThoughtKind::StaffMiserable);
        else if (staff.laziness.value >= t.tired.value)
            out.push_back(ThoughtKind::StaffTired);
        else if (staff.happiness.value >= t.glad.value)
            out.push_back(ThoughtKind::StaffHappy);
        else if (staff.laziness.value <= t.keen.value)
            out.push_back(ThoughtKind::StaffMotivated);
        return out;
    }

    bool quits(const Staff& staff)
    {
        return staff.miserable == kTuning.staff.quit_after + kTuning.staff.leave_after + 1;
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
