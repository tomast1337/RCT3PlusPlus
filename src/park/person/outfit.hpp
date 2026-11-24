// SPDX-License-Identifier: GPL-3.0-or-later
//
// What people are dressed in: the heads, bodies and legs of the install that guests coming in and staff hired wear,
// and the colours their clothes pick from.
#pragma once

#include "core/types.hpp"
#include "save/model/people.hpp"

namespace rct3::park::person
{

    struct PartSet;

    inline constexpr u8 kColourCells = 16; ///< Colours a person's clothes and skin pick from

    /// A head, body or legs that a guest coming in at the gate, or a worker hired, may wear.
    struct Outfit
    {
        save::model::PeopleObjectId id = 0; ///< Of its part group in the park's people
        const PartSet* set = nullptr;       ///< Its meshes, where the install's are read
        save::model::PersonType type = save::model::PersonType::Adult;
        save::model::Sex sex = save::model::Sex::Male;
        u8 section = 0;    ///< As a part group's
        u8 vip = 0;        ///< The celebrity it dresses, by the number VIP goals give them; 0 for guests'
        bool swim = false; ///< Swimwear, worn at a pool in place of the clothes of its section
        u8 costume = 0;    ///< The entertainer's costume it belongs to, by the save's sub type number
    };

} // namespace rct3::park::person
