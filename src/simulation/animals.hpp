// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Animal enclosures at work. Each animal house stands in an enclosure with its troughs and a keeper's hut; the
// keeper hired with it looks after that enclosure, walking about inside it off the paths.
//
// An animal bought into an enclosure comes in a crate, which opens after a while. Out of it, the animal grows
// hungry, thirsty and tired, lonely without others of its kind in the enclosure, and now and then leaves dung.
// It eats and drinks at the troughs of a house in its enclosure, sleeps beside them, and otherwise strolls
// about. Empty troughs, a long thirst or hunger, and dung piling up wear its health down; it mends otherwise.
// The keepers of an enclosure fill its houses' troughs again after a while, or sooner once they run low, and clear
// its dung, each as their tasks allow; one with a patrol fills only the houses it takes in. Each keeper's window
// counts the troughs they filled and the dung they cleared. The park's inspector, if it has one, tells the player
// of a house nobody can fill, of an animal that keeps finding its food trough empty, and of an enclosure deep in
// dung.
//
// An animal standing where there is no enclosure has escaped: one let out of its crate there, or one whose
// enclosure was taken from under it. The park posts a message as it gets out. It roams about, and a keeper
// goes after it, shuts it in a crate where it stands and takes it back to their own enclosure, where the crate
// opens again. Guests keep to the paths, where a loose animal does not scare them off.
//
// A meat-eater hunts the species its species' data names as its prey. Hungry, it runs after the nearest of them in its
// enclosure, which runs from it, and kills it once caught, which feeds it. The park posts a message, guests near enough
// to see it are upset, and the body lies where it fell a while before it is cleared. All the rates are our own.

#include "core/types.hpp"
#include "save/model/people.hpp"
#include "simulation/components.hpp"
#include "simulation/park/messages.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    /// What the animals and their keepers walk over.
    struct AnimalGround
    {
        std::function<f32(glm::vec2)> height;                   ///< Of the ground, at a point across the park
        std::function<std::optional<i32>(glm::vec2)> enclosure; ///< The enclosure a point is in
        /// A point picked at random in enclosure `which`, or with none outside every enclosure; nothing when it
        /// has no ground.
        std::function<std::optional<glm::vec2>(std::optional<i32> which, random::RandomState&)> somewhere;
    };

    /// An animal house, standing in an enclosure.
    /// How often a house's keepers fill its troughs, whether or not they run low. In the order the house's window
    /// lists them, which is also the number an original park keeps.
    enum class FeedingSchedule : u8
    {
        Infrequent,
        Normal,
        Often,
    };

    struct AnimalHouse
    {
        u64 object = 0;                   ///< The house as placed
        u64 hut = 0;                      ///< Its keeper's hut as placed; 0 for none
        std::optional<i32> enclosure { }; ///< The one it stands in
        glm::vec3 troughs { 0.0f };       ///< Where its animals eat, drink and sleep, beside it
        /// The ground the house stands on, across the park from its least corner to its greatest; nobody
        /// strolls to a spot on it.
        glm::vec2 low { 0.0f }, high { 0.0f };
        units::Share food { 1 };      ///< Left in its food trough
        units::Share water { 1 };     ///< Left in its water trough
        units::Seconds since_fed { }; ///< Since its keeper last filled the troughs
        FeedingSchedule schedule { FeedingSchedule::Normal };
    };

    enum class KeeperJob : u8
    {
        Idle,      ///< Stands about
        Strolling, ///< On the way to `goal`, about the enclosure
        Filling,   ///< On the way to the troughs, or filling them
        Clearing,  ///< On the way to dung, or clearing it
        Catching,  ///< After an escaped animal, or taking it home in its crate
        Swapping,  ///< On the way to their house's hut, or changing tools there (animal_care.hpp)
    };

    /// A keeper's work, beside their Staff with the Keeper role.
    struct KeeperDuty
    {
        entt::entity house = entt::null; ///< The house whose enclosure they keep
        KeeperJob job = KeeperJob::Idle;
        glm::vec3 goal { 0.0f };
        entt::entity target = entt::null; ///< The dung they are clearing, or the animal they are catching
        units::Seconds busy { };          ///< Left at the job in hand where it is done, or standing about
    };

    enum class AnimalJob : u8
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
        std::string species { }; ///< The symbol of its species
        std::string overlay { }; ///< The overlay its species is in
        std::string name { };    ///< As the park's messages call it
        bool adult = true;
        bool male = false;
        std::optional<i32> enclosure { }; ///< The one it lives in
        AnimalJob job = AnimalJob::Crated;
        glm::vec3 goal { 0.0f };
        units::Seconds busy { }; ///< Left in its crate, at what it is doing, or standing about
        units::Share hunger { };
        units::Share thirst { };
        units::Share tiredness { };
        units::Share loneliness { };
        units::Share health { 1 };
        units::Seconds dung_in { }; ///< Till it next leaves dung
        i32 empty_troughs = 0;      ///< Times in a row it went hungry to its house and found the food trough empty
        bool escaped = false;       ///< Out of every enclosure
        bool rescued = false;       ///< Bought as a rescued animal, which came in poor health
        units::Seconds flying { };  ///< Left of the helicopter lowering its crate; the crate's own wait starts after
    };

    /// A meat-eater's hunting, beside its Animal.
    struct Hunter
    {
        std::vector<std::string> prey { }; ///< The species it hunts, as their data spells them, in any case
        entt::entity quarry = entt::null;  ///< The animal it is running after
    };

    /// The body of an animal a predator killed, lying where it fell till cleared.
    struct Carcass
    {
        std::string species { }; ///< The symbol of its species
        bool adult = true;
        bool male = false;
        units::Seconds left { }; ///< Till it is cleared
    };

    /// Dung an animal left in its enclosure.
    struct Dung
    {
        std::optional<i32> enclosure { };
        u8 size = 0; ///< Its look, smallest 0 to largest 2
    };

    /// Values for how animals and keepers live.
    struct AnimalRates
    {
        units::Seconds crated { 8 };      ///< In the crate before it opens
        units::Seconds delivery { 6 };    ///< The helicopter lowering a bought animal's crate
        units::Share rescued { 0.3f };    ///< The health a rescued animal comes in with
        units::Pace animal_pace { 1.4f }; ///< An animal walks at
        units::Pace keeper_pace { 1.8f }; ///< And a keeper
        units::Seconds idle { 6 };        ///< Longest anybody stands about between strolls
        units::Seconds hungry { 400 };    ///< From fed to starving
        units::Seconds thirsty { 300 };   ///< From watered to parched
        units::Seconds sleepy { 400 };    ///< From rested to worn out
        units::Seconds lonely { 300 };    ///< From content to lonely, with no others of its kind about
        units::Share wants { 0.5f };      ///< A need this high sends it to the troughs or to sleep
        units::Seconds eating { 6 };      ///< At the troughs
        units::Seconds sleeping { 20 };   ///< Beside them
        units::Share meal { 0.1f };       ///< Of a trough an animal empties as it eats or drinks
        units::Seconds dung { 120 };      ///< Between one dung and the next
        i32 dung_limit = 3;               ///< Dung in an enclosure for each animal in it, beyond which it wears their health down
        units::Seconds sickening { 300 }; ///< From well to dead, starving, parched or among too much dung
        units::Seconds mending { 600 };   ///< From dying to well again, while nothing ails it
        /// Between the keeper's rounds filling the troughs, by FeedingSchedule
        std::array<units::Seconds, 3> feeding { units::Seconds { 600 }, units::Seconds { 300 }, units::Seconds { 150 } };
        units::Share refill { 0.3f };    ///< Either trough this low calls the keeper at once
        units::Seconds filling { 5 };    ///< The keeper at the troughs
        units::Money food { 2500 };      ///< Chosen by eye: charged on Animal food for each filling of the troughs
        units::Seconds clearing { 3 };   ///< The keeper at a dung
        i32 stroll_tries = 8;            ///< Spots picked for a stroll before giving up till later
        units::Distance roam { 4 };      ///< Farthest an escaped animal wanders at a time
        units::Seconds catching { 4 };   ///< The keeper shutting an escaped animal in its crate
        i32 empty_tries = 3;             ///< Times in a row an animal finds its food trough empty before the inspector tells
        units::Pace chase_pace { 3.5f }; ///< A predator runs after its prey at
        units::Pace flee_pace { 2.5f };  ///< And its prey runs from it at
        units::Distance pounce { 0.8f }; ///< A predator this near its prey has caught it
        units::Seconds carcass { 240 };  ///< A killed animal's body lies this long
        units::Distance seen { 20 };     ///< Guests this near a kill see it
    };

    inline constexpr AnimalRates kAnimalRates { };

    /// The dice an animal or keeper `e` of `world` throws.
    [[nodiscard]] Dice dice_for(const SimWorld& world, entt::entity e);

    /// A house for `house` and a keeper for it, standing at `at`. Returns the keeper.
    entt::entity add_animal_house(SimWorld& world, const AnimalHouse& house, glm::vec3 at);

    /// Another keeper for the house `home`, standing at `at`. Returns them.
    entt::entity add_keeper(SimWorld& world, entt::entity home, glm::vec3 at);

    /// `animal` in its crate at `at`. Returns it.
    entt::entity add_animal(SimWorld& world, const Animal& animal, glm::vec3 at);

    /// Lets `animal` out of its crate now, as the player opening it does, unless a keeper is carrying it home or
    /// the helicopter is still lowering it.
    /// Returns whether it came out.
    bool open_crate(SimWorld& world, entt::entity animal);

    /// The message posted as an animal escapes; its subject's name is the animal's.
    inline constexpr std::string_view kAnimalEscaped = "Thought_PI_AnimalEscaped1";
    /// The message posted as a predator kills an animal; its subject's name is the killed animal's.
    inline constexpr std::string_view kAnimalKilled = "WildAnimalMessage_Killed";

    /// A keeper, and the enclosure the park gave them.
    struct KeeperPost
    {
        entt::entity keeper = entt::null;
        i32 enclosure { };
    };

    /// Sends the nearest keeper in `house`'s enclosure who is free (standing about or strolling) and fills troughs to
    /// fill its troughs at once. Returns the keeper, or the one already on the way; entt::null when nobody is free.
    entt::entity call_keeper(SimWorld& world, entt::entity house);

    /// A park's own animal houses, and its keepers: each keeper, with the enclosure the park gave them, keeps
    /// the nearest house in it that no other keeper has yet, else the nearest in it. A keeper with no house in
    /// their enclosure is left without work.
    void import_animal_houses(SimWorld& world, std::span<const AnimalHouse> houses, std::span<const KeeperPost> keepers);

    /// The zoo animals a park was saved with, where they stood, each in the enclosure `enclosure` says its spot is in
    /// and named as `name` makes its saved name in the player's language.
    void import_animals(SimWorld& world, std::span<const save::model::ZooAnimal> saved, const std::function<std::optional<i32>(glm::vec2)>& enclosure, const std::function<std::string(const save::model::ZooAnimal&)>& name);

    /// The dung among the litter a park was saved with, lying where it lay, each in the enclosure `enclosure` says
    /// its spot is in; returns how many pieces.
    usize import_dung(SimWorld& world, std::span<const save::model::Litter> litter, const std::function<std::optional<i32>(glm::vec2)>& enclosure);

    /// Moves the animals and their keepers on by `dt`, posting to `messages` as animals escape or are killed, and leaving
    /// what the park's inspector finds among the Findings.
    void step_animals(SimWorld& world, const AnimalGround& ground, units::Seconds dt, park::ParkMessages& messages);

} // namespace rct3::simulation
