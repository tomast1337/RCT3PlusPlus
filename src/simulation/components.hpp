// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pieces the simulation's entities are made of. Plain values with no behaviour, so systems can read and
// write them in parallel and the park file can store them field by field.
//
// A guest is an entity with a Visitor, a Pose, Needs, a Mood and a Wallet; one the simulation moves also has
// an Errand, a Stride and Dice. An attraction is an entity with an Attraction and either a Shop or a
// FlatRide.
//
// Levels are fractions: needs run from 0 (satisfied) to 1 (desperate); mood values from 0 to 1. Times are
// simulation ticks (see simulation/systems.hpp).
#pragma once

#include "entt/entt.hpp"
#include "math/glm.hpp"
#include "random/generator.hpp"
#include "simulation/path_network.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rct3::simulation
{

    enum class AgeGroup : std::uint8_t
    {
        Child,
        Teen,
        Adult,
    };

    /// Someone visiting the park.
    struct Visitor
    {
        AgeGroup age = AgeGroup::Adult;
        bool female = false;
        std::string name;
        std::uint64_t arrived = 0; ///< Tick the visitor entered the park
    };

    /// Where an entity stands in the world.
    struct Pose
    {
        glm::vec3 position { 0.0f };
        float heading = 0; ///< Radians round the vertical, 0 along +Z
    };

    struct Needs
    {
        float hunger = 0;
        float thirst = 0;
        float bladder = 0;
    };

    struct Mood
    {
        float happiness = 0.5f;
        float energy = 0.5f;
        float nausea = 0;
    };

    struct Wallet
    {
        std::int64_t cash = 0; ///< Hundredths of the currency unit
    };

    // ----- guests on the move -----

    /// What a guest is doing.
    enum class ErrandKind : std::uint8_t
    {
        Wandering, ///< Walking the paths with nowhere in mind
        Heading,   ///< Walking to `target`
        Queueing,  ///< In the queue of the ride `target`
        Riding,    ///< On the ride `target`
    };

    struct Errand
    {
        ErrandKind kind = ErrandKind::Wandering;
        entt::entity target = entt::null; ///< The attraction, when heading for one, queueing or riding
        std::uint32_t rest = 0;           ///< Ticks until the guest next considers what to do
    };

    /// Where a guest is on the paths: on the way from node `from` to node `to`, `progress` of the way along.
    struct Stride
    {
        NodeId from = kNoNode;
        NodeId to = kNoNode;
        float progress = 0;
        float speed = 1.3f;            ///< World units a second
        std::vector<NodeId> route { }; ///< Nodes still to reach after `to`, when heading somewhere
    };

    /// The guest's own random numbers, so each guest's choices do not depend on the order guests are handled in.
    struct Dice
    {
        random::RandomState state;
    };

    // ----- attractions -----

    enum class AttractionKind : std::uint8_t
    {
        Shop,
        FlatRide,
    };

    struct Attraction
    {
        AttractionKind kind = AttractionKind::Shop;
        std::string name;
        NodeId entrance = kNoNode; ///< The path node guests go to for it
        std::int64_t price = 0;    ///< Hundredths of the currency unit
        bool open = true;
        std::int64_t income = 0; ///< Hundredths, taken so far
        std::uint32_t customers = 0;
    };

    enum class Product : std::uint8_t
    {
        Drink,
        Food,
    };

    struct Shop
    {
        Product product = Product::Drink;
    };

    struct FlatRide
    {
        std::uint32_t capacity = 8;           ///< Guests a cycle
        std::uint32_t cycle_ticks = 0;        ///< How long a ride lasts
        std::uint32_t timer = 0;              ///< Ticks left of the cycle running, 0 when stopped
        float excitement = 0.5f;              ///< How much a ride cheers its riders, 0..1
        float intensity = 0.3f;               ///< How much it shakes them, 0..1
        std::vector<entt::entity> queue { };  ///< Guests waiting, first in line first
        std::vector<entt::entity> riders { }; ///< Guests on the ride now
    };

} // namespace rct3::simulation
