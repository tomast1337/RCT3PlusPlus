// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal motion sets: for one animal model, which model animation ("modelanim" item) plays each of the
// motions an animal can make, and how long each lasts. A set has a fixed list of slots, the same for every
// species; a slot a species does not use names no animation.
#pragma once

#include "ovl/overlay.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// The slots of a motion set, by number.
    enum class AnimalMotion : std::uint8_t
    {
        Walk,
        WalkSick,
        Run,
        Leap,
        Stand,
        StandSick,
        Idle1,
        Idle2,
        EatIn,
        EatLoop,
        EatOut,
        DrinkIn,
        DrinkLoop,
        DrinkOut,
        SleepIn,
        SleepLoop,
        SleepOut,
        Scratch,
        InteractionA,
        InteractionB,
        HitObject,
        ClickReaction,
        TurnLeft20 = 25,
        TurnLeft45,
        TurnLeft90,
        TurnRight20,
        TurnRight45,
        TurnRight90,
    };

    struct AnimalMotions
    {
        /// By slot: the model animation's name without the tag, empty where the slot is unused.
        std::vector<std::string_view> animations;
        std::vector<float> seconds; ///< By slot: how long its animation lasts
    };

    struct AnimalMotionsItem
    {
        std::string_view name;                ///< Symbol name without the tag
        std::optional<AnimalMotions> motions; ///< Nothing when the record does not fit its block
    };

    /// Motion sets of one part of an overlay: pointer symbols tagged "wad". The record holds the slot count at
    /// 0x08, then a pointer to that many animation references and one to that many lengths in seconds.
    [[nodiscard]] std::vector<AnimalMotionsItem> read_animal_motions(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
