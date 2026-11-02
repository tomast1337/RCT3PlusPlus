// SPDX-License-Identifier: GPL-3.0-or-later
//
// How content and how lazy staff are, and what the player does about it: training, wages and warnings. Our
// own model; its values are in tuning.hpp. Each staff member is content as their training and pay make them,
// less so when they have far more work than they can do or almost none. An unhappy one grows lazy, more slowly
// the better trained: a lazy one takes longer over each job and stops to rest now and then. A warning shakes
// laziness off, but upsets someone who was working well. Every so often they think about it all: idle or
// worked off their feet, paid too little for it, tired, keen, glad or miserable; one miserable for long
// thinks of quitting, once, and if it goes on longer still, quits.
#pragma once

#include "save/model/people.hpp"
#include "simulation/components.hpp"

#include <cstdint>
#include <vector>

namespace rct3::simulation
{

    inline constexpr std::int32_t kMostTraining = 3;
    inline constexpr std::int32_t kMostWageLevel = 2;

    /// Hundredths a game hour at `level`, 0 to 2.
    [[nodiscard]] std::int64_t wage_at(std::int32_t level);

    /// Sets the wage level, clamped to 0 to 2, and the wage with it.
    void set_wage_level(Staff& staff, std::int32_t level);

    /// What their next level of training costs; 0 once fully trained.
    [[nodiscard]] std::int64_t training_cost(const Staff& staff);

    /// The training a new hire comes with: in a park without money, such as a sandbox, all of it; where money
    /// counts, none, and the park buys each level.
    [[nodiscard]] std::int32_t hired_training(bool park_has_money);

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

    /// Takes up the work the saved worker `saved` had done.
    void take_saved_work(Staff& staff, const save::Staff& saved);

    /// What they think now, at most one thought about their work and one about how they feel; counts the
    /// thoughts in a row they have been miserable.
    [[nodiscard]] std::vector<ThoughtKind> staff_thoughts(Staff& staff);

    /// Whether their last thought was the one that made them quit: true once, however long they stay miserable.
    [[nodiscard]] bool quits(const Staff& staff);

    inline constexpr float kRestSeconds = 20; ///< A rest a lazy staff member takes

} // namespace rct3::simulation
