// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Setting a zoo animal free in the wild. Only a grown animal in good health may go. It leaves the park at once,
// every goal on setting free animals of its kind counts it (the scenario's own and the challenges taken on), and
// the park earns good publicity for it, drawing more parties for a while, longer for a rarer kind.

#include "save/model/park.hpp"
#include "simulation/animals.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

#include <string_view>

namespace rct3::simulation
{

    /// The least health an animal is set free with: the game's own text for a refused release says at least 95%.
    inline constexpr units::Share kReleaseHealth { 0.95f };

    /// Whether `animal` may be set free: grown up, and at least kReleaseHealth healthy.
    [[nodiscard]] bool may_release(const Animal& animal) noexcept;

    /// Counts an animal of `species` set free on every goal on setting free animals of that species: the
    /// scenario's own, and those of challenges taken on and still running.
    void count_release(save::model::ObjectiveSet& goals, std::string_view species);

    /// Sets `animal` of `world` free at calendar time `now`, when may_release lets it go: it is taken out of the
    /// park, counted on the goals (count_release), and its kind's `rarity` (0 common to 1 endangered) draws more
    /// parties for that share of a challenge's one-step publicity prize. Returns whether it went.
    bool release_animal(SimWorld& world, entt::entity animal, f32 rarity, save::model::ObjectiveSet& goals, f32 now);

} // namespace rct3::simulation
