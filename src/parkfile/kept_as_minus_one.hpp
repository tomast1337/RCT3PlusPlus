// SPDX-License-Identifier: GPL-3.0-or-later
//
// The members of the park's models that the park file keeps as a plain number, -1 for none, rather than as
// std::optional's flag and value (see binary.hpp). In memory each is a std::optional, or a std::vector of
// them; parks were saved with these numbers before the models held optionals, so the file keeps them.
// Internal to src/parkfile.
#pragma once

#include "parkfile/saved_park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "simulation/animals.hpp"
#include "simulation/swimming.hpp"

#include <tuple>

namespace rct3::parkfile::detail
{

    /// The members of `T` kept as -1 for none, as pointers to them.
    template<typename T> inline constexpr auto kKeptAsMinusOne = std::tuple { };

    template<> inline constexpr auto kKeptAsMinusOne<save::model::Thought> = std::tuple { &save::model::Thought::time };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::Animation> = std::tuple { &save::model::Animation::anim_type, &save::model::Animation::walk, &save::model::Animation::idle, &save::model::Animation::alternate_walk };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::Staff> = std::tuple { &save::model::Staff::thinks_since_last_task, &save::model::Staff::thinks_since_different_task, &save::model::Staff::thinks_since_no_task, &save::model::Staff::thinks_since_happy };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::ZooAnimal> = std::tuple { &save::model::ZooAnimal::pregnancy_timer };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::ItemHolder> = std::tuple { &save::model::ItemHolder::consuming_slot };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::CarriedItem> = std::tuple { &save::model::CarriedItem::inventory_choice };

    template<> inline constexpr auto kKeptAsMinusOne<save::model::AttractionCommon> = std::tuple { &save::model::AttractionCommon::time_since_inspection, &save::model::AttractionCommon::time_since_breakdown, &save::model::AttractionCommon::time_since_crash };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::Breakdown> = std::tuple { &save::model::Breakdown::station, &save::model::Breakdown::train, &save::model::Breakdown::car, &save::model::Breakdown::seat };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::OperatingOptions> = std::tuple { &save::model::OperatingOptions::min_wait, &save::model::OperatingOptions::max_wait, &save::model::OperatingOptions::fixed_speed };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::RaceState> = std::tuple { &save::model::RaceState::winner };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::TrackPiece> = std::tuple { &save::model::TrackPiece::brake_speed, &save::model::TrackPiece::speed_class };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::Train> = std::tuple { &save::model::Train::station, &save::model::Train::start_station, &save::model::Train::current_station };
    template<> inline constexpr auto kKeptAsMinusOne<save::model::Ride> = std::tuple { &save::model::Ride::train_count, &save::model::Ride::cars_per_train };

    template<> inline constexpr auto kKeptAsMinusOne<save::model::EnclosureMap> = std::tuple { &save::model::EnclosureMap::tiles };

    template<> inline constexpr auto kKeptAsMinusOne<simulation::AnimalHouse> = std::tuple { &simulation::AnimalHouse::enclosure };
    template<> inline constexpr auto kKeptAsMinusOne<simulation::Animal> = std::tuple { &simulation::Animal::enclosure };
    template<> inline constexpr auto kKeptAsMinusOne<simulation::Swimmer> = std::tuple { &simulation::Swimmer::spot, &simulation::Swimmer::place, &simulation::Swimmer::board, &simulation::Swimmer::ladder };

    template<> inline constexpr auto kKeptAsMinusOne<SavedGuest> = std::tuple { &SavedGuest::look, &SavedGuest::party };
    template<> inline constexpr auto kKeptAsMinusOne<SavedWorker> = std::tuple { &SavedWorker::look };
    template<> inline constexpr auto kKeptAsMinusOne<SavedAnimalHouse> = std::tuple { &SavedAnimalHouse::keeper, &SavedAnimalHouse::target };
    template<> inline constexpr auto kKeptAsMinusOne<SavedDung> = std::tuple { &SavedDung::enclosure };

} // namespace rct3::parkfile::detail
