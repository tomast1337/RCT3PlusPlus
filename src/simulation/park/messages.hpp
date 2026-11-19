// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's messages: short news the park posts for the player, such as a new attraction or an award won.
// Each keeps the key of its text and how long ago it was posted. The list holds a fixed number at most,
// dropping the oldest for a new one, and lets each go once it is old enough. Ages count game time, so they
// stand still while the game is paused and run faster at the fast speeds.
//
// A saved park keeps its messages with their ages; the list starts from them.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"
#include "save/model/park.hpp"
#include "units/units.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace rct3::simulation::park
{

    /// Messages kept at most.
    inline constexpr usize kMessageCapacity = 48;
    /// Game time a message is kept, at normal speed twenty minutes.
    inline constexpr units::Seconds kMessageLifetime { 1200 };

    struct ParkMessage
    {
        u64 serial = 0;                      ///< Counts up from 1 in the order messages joined the list
        std::string text_key { };            ///< Names the message's text in the string table
        units::Seconds age { };              ///< Game time since it was posted
        std::optional<u64> subject { };      ///< The object the message is about
        std::string subject_name { };        ///< Its name when the message was posted; may be empty
        std::optional<i32> panel { };        ///< The info panel's tab its subject opens at, by number (ui::screens::info::kMessageTabs); none or negative keeps the last
        std::optional<i32> award { };        ///< Award messages: which award
        std::string field_key { };           ///< Text key filling its other field, such as a breakdown's kind; none drops the field
        std::optional<i64> number { };       ///< Fills the %d in its text, such as how many a crash hurt
        std::optional<glm::vec3> place { };  ///< A spot in the park it points out, such as where a test train stalled; its camera goes there
        std::optional<usize> build_item { }; ///< An item of the build lists it is about, such as an invention, by its place in them
    };

    struct ParkMessages
    {
        std::deque<ParkMessage> list; ///< Oldest first
        u64 last_serial = 0;
    };

    /// The list a saved park starts with: its messages oldest first, the newest kept when there are too many.
    [[nodiscard]] ParkMessages park_messages(const save::model::MessageLog& log);

    /// Adds `message` as the newest, numbering it, and drops the oldest when the list is full.
    void post(ParkMessages& messages, ParkMessage message);

    /// Ages every message by `game_seconds` and drops those past their lifetime.
    void age(ParkMessages& messages, f32 game_seconds);

} // namespace rct3::simulation::park
