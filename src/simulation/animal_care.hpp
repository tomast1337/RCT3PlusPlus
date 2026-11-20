// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Animal care: what keeps a zoo animal well beyond food, water, sleep and clean ground (animals.hpp).
//
// Each species eats only at its own kind of animal house. An animal with no house of its kind in its enclosure,
// one that finds every trough of its house taken, and one that cannot climb the ground on the way to it go hungry,
// and the park's inspector tells of each. The enclosure is the animal's habitat: its room against the room each
// animal in it wants by its species, and the plants on its ground. An animal wants others of its kind about it, the
// more the more sociable it is, and grows lonely with fewer; one that wants at most one of them is annoyed by a crowd
// of other animals. Poor habitat, loneliness and annoyance each keep its health from mending all the way. Animals of
// a species differ a little in appetite and sociability. An animal left in very poor health for a while brings a
// warning that it will be confiscated, and is taken away if it stays so.
//
// A keeper fetches the tool for the job from their house's hut as they change from filling troughs to clearing dung
// or back; one who cannot reach the hut works on with the tool in hand, and the inspector tells. Trained keepers
// work faster.

#include "core/types.hpp"
#include "ovl/animal/species.hpp"
#include "random/generator.hpp"
#include "simulation/animals.hpp"
#include "simulation/park/messages.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

#include <glm/vec3.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rct3::simulation
{

    /// An animal's care, beside its Animal.
    struct AnimalCare
    {
        std::string house { };          ///< The symbol of the house its species eats at, in any case; empty for any house
        i32 space = 0;                  ///< Tiles of enclosure it wants; 0 while its species is not known, which never lacks room
        i32 appetite = 10;              ///< 1 to 20: the more, the sooner it is hungry again
        i32 sociability = 10;           ///< 0 to 20: the more, the more of its kind it wants about it
        units::Share habitat { 1 };     ///< Its enclosure, for it: the room and the plants
        units::Share most_health { 1 }; ///< As far as its habitat and company let its health mend
        bool annoyed = false;           ///< By a crowd of other animals
        units::Seconds neglected { };   ///< So far in very poor health
        bool warned = false;            ///< That it will be confiscated
        i32 turned_away = 0;            ///< Times in a row it found every trough of its house taken
        i32 out_of_reach = 0;           ///< Times in a row it found no way up to its house
    };

    /// An animal house's care, beside its AnimalHouse.
    struct HouseCare
    {
        std::string kind { };             ///< Its symbol, which the species eating at it name, in any case; empty feeds any
        std::optional<glm::vec3> hut { }; ///< Where its keepers fetch their tools; none for a house without a hut
    };

    /// A keeper's tool, beside their KeeperDuty: the job it is for, Filling or Clearing.
    struct KeeperTool
    {
        KeeperJob holding = KeeperJob::Filling;
        KeeperJob fetching = KeeperJob::Filling; ///< The job they go on to while Swapping
    };

    /// An enclosure's ground, as the habitat of the animals in it.
    struct EnclosureSite
    {
        i32 tiles = 0;
        i32 plants = 0; ///< Trees, shrubs and flowers standing on it
    };

    /// Values for animal care. Chosen by eye: each one picked to read well in play, none from a source.
    struct AnimalCareRates
    {
        i32 places = 4;                        ///< Animals a house feeds at once
        i32 tries = 3;                         ///< Times in a row an animal goes without its house before the inspector tells
        f32 steepest = 1.5f;                   ///< Rise over run an animal climbs on the way to its house
        units::Distance stride { 0.5f };       ///< Between the heights looked at on that way
        units::Share more_room { 0.25f };      ///< Of its species' space each animal beyond the first of it wants
        f32 plants_per_tile = 0.2f;            ///< Plants for each tile of an enclosure fully planted
        units::Share planting { 0.2f };        ///< Of a habitat that its plants make up; its room the rest
        units::Share habitat_weight { 0.35f }; ///< Of health that a habitat of nothing keeps an animal from
        units::Share lonely_weight { 0.3f };   ///< Likewise being fully lonely
        units::Share annoyed_weight { 0.25f }; ///< Likewise being annoyed
        i32 per_companion = 6;                 ///< Sociability for each other of its kind an animal wants about it, so 3 at most
        i32 crowd = 3;                         ///< Other animals one wanting at most one companion puts up with
        units::Share lonely_told { 0.8f };     ///< Loneliness the inspector tells of
        units::Share neglect { 0.15f };        ///< Health below which an animal is neglected
        units::Seconds warning { 300 };        ///< Neglected this long, the park is warned
        units::Seconds confiscation { 600 };   ///< And this long, the animal is taken away
        i32 spread = 3;                        ///< Farthest an animal's appetite or sociability lies from its species'
        units::Seconds swapping { 3 };         ///< At the hut, changing tools
        f32 untrained = 1.6f;                  ///< How much longer an untrained keeper takes at a job than a fully trained one
    };

    inline constexpr AnimalCareRates kAnimalCareRates { };

    /// Posted as an animal has been neglected a while, and as it is taken away; the subject's name is the animal's.
    inline constexpr std::string_view kAboutToBeConfiscated = "WildAnimalMessage_AboutToBeConfiscated";
    inline constexpr std::string_view kConfiscated = "WildAnimalMessage_Confiscated";

    /// The care of a new animal of a species with `species`' care, eating at the house `house`: its appetite and
    /// sociability near the species', by `dice`.
    [[nodiscard]] AnimalCare care_for(const ovl::animal::SpeciesCare& species, std::string_view house, random::RandomState& dice);

    /// Whether the animal `animal` of `r` eats at the house `house`: one of its species' kind, or any for an animal or
    /// house whose kind is not known.
    [[nodiscard]] bool eats_at(const entt::registry& r, entt::entity animal, entt::entity house);

    /// Others of its kind the animal `animal` of `r` wants about it.
    [[nodiscard]] i32 wanted_company(const entt::registry& r, entt::entity animal);

    /// From fed to starving, for the animal `animal` of `r`, by its appetite.
    [[nodiscard]] units::Seconds hungry_after(const entt::registry& r, entt::entity animal);

    /// As far as the animal `animal` of `r` mends.
    [[nodiscard]] units::Share most_health(const entt::registry& r, entt::entity animal);

    /// Whether the way from `from` to `to` over `ground` is never too steep to climb.
    [[nodiscard]] bool reaches(const AnimalGround& ground, glm::vec3 from, glm::vec3 to);

    /// Whether the hungry or thirsty animal `animal` of `r` may go to eat at the house `house` now: whether it finds a
    /// trough free and a way up to it over `ground`. Telling the park's `inspector` (null for none) once it went
    /// without a few times in a row; `now` is the game time.
    [[nodiscard]] bool welcome_at(entt::registry& r, const AnimalGround& ground, entt::entity animal, entt::entity house, entt::entity inspector, f32 now);

    /// Sends the keeper `keeper` of `r`, just set Filling or Clearing, over `ground` to their house's hut first to swap
    /// tools when they hold the other one. A keeper who cannot reach the hut works on with the tool in hand, and the
    /// park's `inspector` (null for none) tells of it; `now` is the game time.
    void fetch_tool(entt::registry& r, const AnimalGround& ground, entt::entity keeper, entt::entity inspector, f32 now);

    /// The keeper `keeper` of `r`, done Swapping at the hut, goes on to the job they fetched the tool for; idle when
    /// what they were after is gone.
    void tool_swapped(entt::registry& r, entt::entity keeper);

    /// How long a keeper with `training` (0 to staff::kMostTraining) takes at a job a fully trained one does in `job`.
    [[nodiscard]] units::Seconds at_training(units::Seconds job, i32 training);

    /// How long the keeper `keeper` of `r` takes filling troughs (`job` Filling) or clearing dung, by their training.
    [[nodiscard]] units::Seconds job_time(const entt::registry& r, entt::entity keeper, KeeperJob job);

    /// Weighs each animal's habitat in `sites` (by enclosure) and company, and so how far its health mends; warns of
    /// and takes away animals neglected too long, posting to `messages`; and leaves what the park's inspector finds
    /// among the Findings.
    void step_animal_care(SimWorld& world, std::span<const EnclosureSite> sites, units::Seconds dt, park::ParkMessages& messages);

} // namespace rct3::simulation
