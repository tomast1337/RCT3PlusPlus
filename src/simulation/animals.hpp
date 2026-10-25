// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Animal enclosures at work. Each animal house stands in an enclosure with its troughs and a keeper's hut; the
// keeper hired with it looks after that enclosure, walking about inside it off the paths.

#include "simulation/sim_world.hpp"
#include "simulation/units.hpp"

#include <cstdint>
#include <functional>
#include <optional>

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
        glm::vec3 troughs { 0.0f };  ///< Where its animals eat and drink
        /// The ground the house stands on, across the park from its least corner to its greatest; nobody
        /// strolls to a spot on it.
        glm::vec2 low { 0.0f }, high { 0.0f };
    };

    /// What a keeper is doing.
    enum class KeeperJob : std::uint8_t
    {
        Idle,    ///< Stands about, or strolls round the enclosure
        Walking, ///< On the way to `goal`
    };

    /// A keeper's work, beside their Staff with the Keeper role.
    struct KeeperDuty
    {
        entt::entity house = entt::null; ///< The house whose enclosure they keep
        KeeperJob job = KeeperJob::Idle;
        glm::vec3 goal { 0.0f };
        Seconds busy { }; ///< Left standing about
    };

    inline constexpr float kKeeperPace = 1.1f;        ///< World units a second a keeper walks
    inline constexpr float kKeeperIdleSeconds = 6.0f; ///< Longest a keeper stands about between strolls
    inline constexpr int kStrollTries = 8;            ///< Spots picked for a stroll before giving up till later

    /// A house for `house` and a keeper for it, standing at `at`. Returns the keeper.
    entt::entity add_animal_house(SimWorld& world, const AnimalHouse& house, glm::vec3 at);

    /// Moves the keepers on by `dt`.
    void step_animals(SimWorld& world, const AnimalGround& ground, Seconds dt);

} // namespace rct3::simulation
