// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Animal enclosures at work. Each animal house stands in an enclosure with its troughs and a keeper's hut; the
// keeper hired with it looks after that enclosure, walking about inside it off the paths.
//
// An animal bought into an enclosure comes in a crate, which opens after a while. Out of it, the animal grows
// hungry, thirsty and tired, lonely without others of its kind in the enclosure, and now and then leaves dung.
// It eats and drinks at the troughs of a house in its enclosure, sleeps beside them, and otherwise strolls
// about. Empty troughs, a long thirst or hunger, and dung piling up wear its health down; it mends otherwise.
// The keeper fills the troughs again after a while, or sooner once they run low, and clears the dung.
//
// An animal standing where there is no enclosure has escaped: one let out of its crate there, or one whose
// enclosure was taken from under it. The park posts a message as it gets out. It roams about, and a keeper
// goes after it, shuts it in a crate where it stands and takes it back to their own enclosure, where the crate
// opens again. Guests keep to the paths, where a loose animal does not scare them off. All the rates are our own.

#include "simulation/components.hpp"
#include "simulation/park_messages.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/units.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::simulation
{

    /// What the animals and their keepers walk over.
    struct AnimalGround
    {
        std::function<float(glm::vec2)> height;           ///< Of the ground, at a point across the park
        std::function<std::int32_t(glm::vec2)> enclosure; ///< The enclosure a point is in; -1 in none
        /// A point picked at random in enclosure `which`; nothing when it has no ground.
        std::function<std::optional<glm::vec2>(std::int32_t which, random::RandomState&)> somewhere;
    };

    /// An animal house, standing in an enclosure.
    struct AnimalHouse
    {
        std::uint64_t object = 0;    ///< The house as placed
        std::uint64_t hut = 0;       ///< Its keeper's hut as placed; 0 for none
        std::int32_t enclosure = -1; ///< The one it stands in
        glm::vec3 troughs { 0.0f };  ///< Where its animals eat, drink and sleep, beside it
        /// The ground the house stands on, across the park from its least corner to its greatest; nobody
        /// strolls to a spot on it.
        glm::vec2 low { 0.0f }, high { 0.0f };
        Share food { 1 };      ///< Left in its food trough
        Share water { 1 };     ///< Left in its water trough
        Seconds since_fed { }; ///< Since its keeper last filled the troughs
    };

    /// What a keeper is doing.
    enum class KeeperJob : std::uint8_t
    {
        Idle,      ///< Stands about
        Strolling, ///< On the way to `goal`, about the enclosure
        Filling,   ///< On the way to the troughs, or filling them
        Clearing,  ///< On the way to dung, or clearing it
        Catching,  ///< After an escaped animal, or taking it home in its crate
    };

    /// A keeper's work, beside their Staff with the Keeper role.
    struct KeeperDuty
    {
        entt::entity house = entt::null; ///< The house whose enclosure they keep
        KeeperJob job = KeeperJob::Idle;
        glm::vec3 goal { 0.0f };
        entt::entity target = entt::null; ///< The dung they are clearing, or the animal they are catching
        Seconds busy { };                 ///< Left at the job in hand where it is done, or standing about
    };

    /// What an animal is doing.
    enum class AnimalJob : std::uint8_t
    {
        Crated,    ///< Still in the crate it came in
        Idle,      ///< Stands about
        Strolling, ///< On the way to `goal`
        Feeding,   ///< On the way to the troughs, or eating and drinking there
        Sleeping,  ///< On the way to the troughs, or asleep beside them
    };

    /// An animal of a species, bought into an enclosure.
    struct Animal
    {
        std::string species { };     ///< The symbol of its species
        std::string overlay { };     ///< The overlay its species is in
        std::string name { };        ///< As the park's messages call it
        std::int32_t enclosure = -1; ///< The one it lives in
        AnimalJob job = AnimalJob::Crated;
        glm::vec3 goal { 0.0f };
        Seconds busy { }; ///< Left in its crate, at what it is doing, or standing about
        Share hunger { };
        Share thirst { };
        Share tiredness { };
        Share loneliness { };
        Share health { 1 };
        Seconds dung_in { };  ///< Till it next leaves dung
        bool escaped = false; ///< Out of every enclosure
    };

    /// Dung an animal left in its enclosure.
    struct Dung
    {
        std::int32_t enclosure = -1;
    };

    /// Values for how animals and keepers live.
    struct AnimalRates
    {
        Seconds crated { 8 };      ///< In the crate before it opens
        float animal_pace = 0.8f;  ///< World units a second an animal walks
        float keeper_pace = 1.1f;  ///< And a keeper
        Seconds idle { 6 };        ///< Longest anybody stands about between strolls
        Seconds hungry { 240 };    ///< From fed to starving
        Seconds thirsty { 180 };   ///< From watered to parched
        Seconds sleepy { 400 };    ///< From rested to worn out
        Seconds lonely { 300 };    ///< From content to lonely, with no others of its kind about
        Share wants { 0.5f };      ///< A need this high sends it to the troughs or to sleep
        Seconds eating { 6 };      ///< At the troughs
        Seconds sleeping { 20 };   ///< Beside them
        Share meal { 0.1f };       ///< Of a trough an animal empties as it eats or drinks
        Seconds dung { 120 };      ///< Between one dung and the next
        int dung_limit = 3;        ///< Dung in its enclosure beyond this wears its health down
        Seconds sickening { 300 }; ///< From well to dead, starving, parched or among too much dung
        Seconds mending { 600 };   ///< From dying to well again, while nothing ails it
        Seconds feeding { 300 };   ///< Between the keeper's rounds filling the troughs
        Share refill { 0.3f };     ///< Either trough this low calls the keeper at once
        Seconds filling { 5 };     ///< The keeper at the troughs
        Seconds clearing { 3 };    ///< The keeper at a dung
        int stroll_tries = 8;      ///< Spots picked for a stroll before giving up till later
        float roam = 4;            ///< Farthest an escaped animal wanders at a time, in world units
        Seconds catching { 4 };    ///< The keeper shutting an escaped animal in its crate
    };

    inline constexpr AnimalRates kAnimalRates { };

    /// The dice an animal or keeper `e` of `world` throws.
    [[nodiscard]] Dice dice_for(const SimWorld& world, entt::entity e);

    /// A house for `house` and a keeper for it, standing at `at`. Returns the keeper.
    entt::entity add_animal_house(SimWorld& world, const AnimalHouse& house, glm::vec3 at);

    /// `animal` in its crate at `at`. Returns it.
    entt::entity add_animal(SimWorld& world, const Animal& animal, glm::vec3 at);

    /// The message posted as an animal escapes; its subject's name is the animal's.
    inline constexpr std::string_view kAnimalEscaped = "Thought_PI_AnimalEscaped1";

    /// Moves the animals and their keepers on by `dt`, posting to `messages` as animals escape.
    void step_animals(SimWorld& world, const AnimalGround& ground, Seconds dt, ParkMessages& messages);

} // namespace rct3::simulation
