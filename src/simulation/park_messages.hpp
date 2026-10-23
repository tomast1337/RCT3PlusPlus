// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's messages: short news the park posts for the player, such as a new attraction or an award won.
// Each keeps the key of its text and how long ago it was posted. The list holds a fixed number at most,
// dropping the oldest for a new one, and lets each go once it is old enough. Ages count game time, so they
// stand still while the game is paused and run faster at the fast speeds.
//
// A saved park keeps its messages with their ages; the list starts from them.
#pragma once

#include "save/model/park.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace rct3::simulation
{

    /// Messages kept at most.
    inline constexpr std::size_t kMessageCapacity = 48;
    /// Game seconds a message is kept, at normal speed twenty minutes.
    inline constexpr float kMessageLifetime = 1200;

    struct ParkMessage
    {
        std::uint64_t serial = 0;             ///< Counts up from 1 in the order messages joined the list
        std::string text_key;                 ///< Names the message's text in the string table
        float age = 0;                        ///< Game seconds since it was posted
        std::optional<std::uint64_t> subject; ///< The object the message is about
        std::string subject_name;             ///< Its name when the message was posted; may be empty
        std::optional<std::int32_t> panel;    ///< The info panel's tab its subject opens at, for a ride as ui::AttractionTab; none or negative keeps the last
        std::optional<std::int32_t> award;    ///< Award messages: which award
    };

    struct ParkMessages
    {
        std::deque<ParkMessage> list; ///< Oldest first
        std::uint64_t last_serial = 0;
    };

    /// The list a saved park starts with: its messages oldest first, the newest kept when there are too many.
    [[nodiscard]] ParkMessages park_messages(const save::MessageLog& log);

    /// Adds `message` as the newest, numbering it, and drops the oldest when the list is full.
    void post(ParkMessages& messages, ParkMessage message);

    /// Ages every message by `game_seconds` and drops those past their lifetime.
    void age(ParkMessages& messages, float game_seconds);

} // namespace rct3::simulation
