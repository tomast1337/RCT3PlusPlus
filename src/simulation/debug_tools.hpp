// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulation side of the developer windows' Debug tab: one-step changes that put the simulated park in a
// state at once, so a case can be set up without waiting for the park to get there. Each keeps the park whole:
// what points at what it changes (queues, riders, parties, keepers' tasks) is put right with it.
#pragma once

#include "core/types.hpp"
#include "random/generator.hpp"
#include "simulation/path_network.hpp"
#include "simulation/research.hpp"
#include "units/units.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace rct3::simulation
{

    struct SimWorld;

    /// Invents every item of `items` at once, at `time` of the calendar, in their order; `items` is left empty and
    /// nothing is worked on or focused.
    void invent_everything(Research& research, std::vector<ResearchItem>& items, f32 time);

    /// Brings in parties as the gate does, each on a tile of `spots` picked with `dice`, until at least `count`
    /// guests have come. Returns how many came; none without spots.
    usize add_guests(SimWorld& world, random::RandomState& dice, std::span<const NodeId> spots, usize count);

    /// Sends every guest home at once, from wherever they are: the queues and rides are emptied and the parties
    /// broken up, and each guest is gone as one who walked out of the gate is. Returns how many went.
    usize send_guests_home(SimWorld& world);

    /// Meets every guest's needs: not hungry, thirsty or needing a toilet or a swim, happy, rested, well and dry.
    /// Returns how many guests.
    usize refresh_guests(SimWorld& world);

    /// Breaks down every ride that is not broken already, closing it till a mechanic repairs it. Returns how many
    /// broke.
    usize break_down_rides(SimWorld& world);

    /// Puts every ride back as new: repaired and opened again if a breakdown had closed it, fully reliable and just
    /// inspected, its queues emptied (those in them walk off from its entrance) and its counts back to nought.
    /// Returns how many rides.
    usize reset_rides(SimWorld& world);

    /// Takes every zoo animal out of the park; keepers after one go back to their other work. Returns how many.
    usize remove_animals(SimWorld& world);

    /// Meets every zoo animal's needs and makes it well again. Returns how many animals.
    usize refresh_animals(SimWorld& world);

    /// Makes every adult female zoo animal out of her crate and not yet carrying young pregnant with one, born after
    /// `due`, by an adult male of her kind in her enclosure if there is one. Returns how many.
    usize make_animals_pregnant(SimWorld& world, units::Seconds due);

    /// Drops `count` pieces of litter, each on a tile of `spots` picked with `dice`. Returns how many; none without
    /// spots.
    usize scatter_litter(SimWorld& world, random::RandomState& dice, std::span<const NodeId> spots, usize count);

    /// Takes away all the litter and sick lying on the paths. Returns how many pieces.
    usize clear_litter(SimWorld& world);

} // namespace rct3::simulation
