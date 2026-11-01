// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a thought a guest had reads as. A saved thought is a number naming what it is about and a random
// number that picks one of the ways it can be said: each kind of thought has a few texts in the language
// file, numbered from 1, and the random number taken modulo how many there are picks one. A text may name
// the thing thought about, where it has %NAME%. Each thought also has a mood, shown as a face before it: a
// grinning one for a glad thought, a red frowning one for a complaint, a plain one for the rest.
#pragma once

#include "ui/string_table.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::ui
{

    enum class Mood : std::uint8_t
    {
        Neutral,
        Happy,
        Sad,
    };

    /// The start of the language keys of the thought `id`'s texts; empty for no thought or an unknown one.
    [[nodiscard]] std::string thought_key(std::int32_t id);

    /// The text of the thought `id` said in the way `variant` picks, about `name`. Empty when the language has
    /// no text for it.
    [[nodiscard]] std::u16string thought_text(const StringTable& strings, std::int32_t id, std::int64_t variant, std::u16string_view name);

    /// How the thought `id` feels; neutral for no thought or an unknown one.
    [[nodiscard]] Mood thought_mood(std::int32_t id);
    /// The mood of the thought whose text `key` is, such as "Thought_ImLost2"; none for a key of no thought.
    [[nodiscard]] std::optional<Mood> key_mood(std::string_view key);
    /// The skin of the face standing for `mood`.
    [[nodiscard]] std::string mood_icon(Mood mood);

} // namespace rct3::ui
