// SPDX-License-Identifier: GPL-3.0-or-later
//
// What people are doing, by number. A saved person names the motion it is playing, and the ones it walks
// and idles with, by a number shared by every model; each model's meshes name their motions by text, in
// any case and in their own order. This table ties the two together. Several spellings can share a
// number (an old and a new name for the same motion).
//
// Most motions repeat. The rest play once and then hold their last pose: getting into a ride, falling over,
// the start and end of a cheer.
#pragma once

#include "core/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::park::person
{

    /// Numbers of a few motions the game picks on its own.
    namespace motion
    {
        inline constexpr i32 kWalk = 0;
        inline constexpr i32 kIdle = 35;
    } // namespace motion

    /// The number of the motion named `name`, whatever its case. Nothing for a name not in the table.
    [[nodiscard]] std::optional<i32> motion_number(std::string_view name);

    /// A motion's number and one of its names.
    struct NamedMotion
    {
        i32 number = 0;
        std::string_view name { };
    };

    /// Every motion the table knows, by number, each once.
    [[nodiscard]] std::vector<NamedMotion> motions_by_number();

    /// How a rider seated in a ride car moves: riding along, waiting at the station or up a lift, with their
    /// hands up, covering their eyes, and pressed hard by the ride. Each by number; those a seating has no
    /// motion for are its riding one.
    struct RiderMotions
    {
        i32 ride = 0;
        i32 wait = 0;
        i32 hands_up = 0;
        i32 cover_eyes = 0;
        i32 strained = 0;
        /// Played once on raising their hands or covering their eyes and on bringing them down; nothing where
        /// the seating has none.
        std::optional<i32> hands_up_in { };
        std::optional<i32> hands_up_out { };
        std::optional<i32> cover_eyes_in { };
        std::optional<i32> cover_eyes_out { };
    };

    /// A motion and a length of time: how long it plays, or how far into it.
    struct Clip
    {
        i32 motion = 0;
        f32 seconds = 0;
    };

    /// Of `clips` played in turn, each for its time and the last for good, the one playing `since` seconds after
    /// the first began and how far into it. `clips` must not be empty.
    [[nodiscard]] Clip clip_at(std::span<const Clip> clips, f32 since);

    /// The motions of riders seated as `seating` says (a ride car's or a flat ride's seating number); nothing for a seating
    /// not known.
    [[nodiscard]] std::optional<RiderMotions> rider_motions(u8 seating);

    inline constexpr u8 kSlideBodySeating = 30; ///< A body slide's rider, lying on their back
    inline constexpr u8 kSlideRingSeating = 31; ///< A ring slide's rider, sitting in the ring

    /// Whether motion `number` starts again from the beginning once it ends.
    [[nodiscard]] bool motion_repeats(i32 number);

} // namespace rct3::park::person