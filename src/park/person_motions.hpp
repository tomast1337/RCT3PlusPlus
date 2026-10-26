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
    };

    /// The motions of riders seated as `seating` says (a ride car's seating number); nothing for a seating
    /// not known.
    [[nodiscard]] std::optional<RiderMotions> rider_motions(std::uint8_t seating);

    /// Whether motion `number` starts again from the beginning once it ends.
    [[nodiscard]] bool motion_repeats(std::int32_t number);

} // namespace rct3::park
