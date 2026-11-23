// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The goals of zoo parks: animals of a species kept, profit from selling them, animals set free, escaped animals
// caught again, and the viewing galleries' number, visitors and takings.

#include "save/model/park.hpp"
#include "simulation/objectives.hpp"

namespace rct3::simulation
{
    /// Whether goals of `kind` are zoo goals, read by read_zoo_goal.
    [[nodiscard]] bool zoo_goal(save::model::ConditionKind kind) noexcept;

    /// Reads zoo goal `c` against `figures`.
    /// - Animals of a species: those in the park, crated ones too; bought ones count unless the goal asks for
    ///   animals born in the park only.
    /// - Profit from selling a species' animals: what selling them brought in less what buying them cost, over every
    ///   month the park has had.
    /// - Animals set free and escaped animals caught: the counts the goal keeps. A challenge to catch escaped animals
    ///   is not met before they have got out.
    /// - Galleries built, their visitors, and last month's gallery tickets.
    [[nodiscard]] GoalReading read_zoo_goal(const save::model::Condition& c, const ParkFigures& figures);

} // namespace rct3::simulation
