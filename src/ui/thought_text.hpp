// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a thought a guest had reads as. A saved thought is a number naming what it is about and a random
// number that picks one of the ways it can be said: each kind of thought has a few texts in the language
// file, numbered from 1, and the random number taken modulo how many there are picks one. A text may name
// the thing thought about, where it has %NAME%. Each thought also has a mood, shown as a face before it: a
// grinning one for a glad thought, a red frowning one for a complaint, a plain one for the rest.
#pragma once

#include "core/types.hpp"
#include "ui/string_table.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::ui
{

    enum class Mood : u8
    {
        Neutral,
        Happy,
        Sad,
    };

    /// The start of the language keys of the thought `id`'s texts; empty for no thought or an unknown one.
    [[nodiscard]] std::string thought_key(i32 id);

    /// The language key of the text of the thought `id` said in the way `variant` picks, such as "Thought_ImLost2";
    /// empty when the language has no text for it.
    [[nodiscard]] std::string thought_text_key(const StringTable& strings, i32 id, i64 variant);

    /// The text of the thought `id` said in the way `variant` picks, about `name`. Empty when the language has
    /// no text for it.
    [[nodiscard]] std::u16string thought_text(const StringTable& strings, i32 id, i64 variant, std::u16string_view name);

    /// How the thought `id` feels; neutral for no thought or an unknown one.
    [[nodiscard]] Mood thought_mood(i32 id);
    /// The mood of the thought whose text `key` is, such as "Thought_ImLost2"; none for a key of no thought.
    [[nodiscard]] std::optional<Mood> key_mood(std::string_view key);
    /// How someone as happy as `happiness` (0 to 1) feels: the scale cut in thirds, sad in the lowest and happy in the
    /// highest.
    [[nodiscard]] Mood happiness_mood(f32 happiness);
    /// The skin of the face standing for `mood`.
    [[nodiscard]] std::string mood_icon(Mood mood);

} // namespace rct3::ui
