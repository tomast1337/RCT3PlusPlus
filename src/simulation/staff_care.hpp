// SPDX-License-Identifier: GPL-3.0-or-later
//
// How content and how lazy staff are, and what the player does about it: training, wages and warnings. Our
// own model and values. Each staff member is content as their training and pay make them, less so when they
// have far more work than they can do or almost none. An unhappy one grows lazy, more slowly the better
// trained: a lazy one takes longer over each job and stops to rest now and then. A warning shakes laziness
// off, but upsets someone who was working well.
#pragma once

#include "simulation/components.hpp"

#include <array>
#include <cstdint>

namespace rct3::simulation
{

    inline constexpr std::int32_t kMostTraining = 3;
    inline constexpr std::int32_t kMostWageLevel = 2;
    /// What each level of training costs, in hundredths, from untrained up. Our own values.
    inline constexpr std::array<std::int64_t, kMostTraining> kTrainingCost { 5000, 10000, 20000 };

    /// Hundredths a game hour at `level`, 0 to 2.
    [[nodiscard]] std::int64_t wage_at(std::int32_t level);

    /// Sets the wage level, clamped to 0 to 2, and the wage with it.
    void set_wage_level(Staff& staff, std::int32_t level);

    /// What their next level of training costs; 0 once fully trained.
    [[nodiscard]] std::int64_t training_cost(const Staff& staff);

    /// Trains them one level up; false when they already are fully trained.
    bool train(Staff& staff);

    /// A warning: laziness gone if they had grown lazy, else unhappier.
    void warn(Staff& staff);

    /// `seconds` more of their mood: `working` whether they were at a job then.
    void care_for(Staff& staff, bool working, float seconds);

    /// How many times as long a job takes them.
    [[nodiscard]] float work_slowdown(const Staff& staff);

    /// The chance, a second, that they stop to rest while they have nothing to do.
    [[nodiscard]] float rest_chance(const Staff& staff);

    inline constexpr float kRestSeconds = 20; ///< A rest a lazy staff member takes; our own value

} // namespace rct3::simulation
