// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pieces the simulation's entities are made of. Plain values with no behaviour, so systems can read and
// write them in parallel and the park file can store them field by field. A guest is an entity with a
// Visitor, a Pose, Needs, a Mood and a Wallet.
//
// Levels are fractions: needs run from 0 (satisfied) to 1 (desperate); mood values from 0 to 1.
#pragma once

#include "math/glm.hpp"

#include <cstdint>
#include <string>

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

} // namespace rct3::simulation
