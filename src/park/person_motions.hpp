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

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace rct3::park
{

    /// Numbers of a few motions the game picks on its own.
    namespace motion
    {
        inline constexpr std::int32_t kWalk = 0;
        inline constexpr std::int32_t kIdle = 35;
    } // namespace motion

    /// The number of the motion named `name`, whatever its case. Nothing for a name not in the table.
    [[nodiscard]] std::optional<std::int32_t> motion_number(std::string_view name);

    /// How a rider seated in a ride car moves: riding along, waiting at the station or up a lift, with their
    /// hands up, covering their eyes, and pressed hard by the ride. Each by number; those a seating has no
    /// motion for are its riding one.
    struct RiderMotions
    {
        std::int32_t ride = 0;
        std::int32_t wait = 0;
        std::int32_t hands_up = 0;
        std::int32_t cover_eyes = 0;
        std::int32_t strained = 0;
        /// Played once on raising their hands or covering their eyes and on bringing them down; nothing where
        /// the seating has none.
        std::optional<std::int32_t> hands_up_in { };
        std::optional<std::int32_t> hands_up_out { };
        std::optional<std::int32_t> cover_eyes_in { };
        std::optional<std::int32_t> cover_eyes_out { };
    };

    /// A motion and a length of time: how long it plays, or how far into it.
    struct Clip
    {
        std::int32_t motion = 0;
        float seconds = 0;
    };

    /// Of `clips` played in turn, each for its time and the last for good, the one playing `since` seconds after
    /// the first began and how far into it. `clips` must not be empty.
    [[nodiscard]] Clip clip_at(std::span<const Clip> clips, float since);

    /// The motions of riders seated as `seating` says (a ride car's or a flat ride's seating number); nothing for a seating
    /// not known.
    [[nodiscard]] std::optional<RiderMotions> rider_motions(std::uint8_t seating);

    /// Whether motion `number` starts again from the beginning once it ends.
    [[nodiscard]] bool motion_repeats(std::int32_t number);

} // namespace rct3::park
