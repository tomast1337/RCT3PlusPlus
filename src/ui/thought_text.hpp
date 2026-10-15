// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a thought a guest had reads as. A saved thought is a number naming what it is about and a random
// number that picks one of the ways it can be said: each kind of thought has a few texts in the language
// file, numbered from 1, and the random number taken modulo how many there are picks one. A text may name
// the thing thought about, where it has %NAME%.
#pragma once

#include "ui/string_table.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace rct3::ui
{

    /// The start of the language keys of the thought `id`'s texts; empty for no thought or an unknown one.
    [[nodiscard]] std::string thought_key(std::int32_t id);

    /// The text of the thought `id` said in the way `variant` picks, about `name`. Empty when the language has
    /// no text for it.
    [[nodiscard]] std::u16string thought_text(const StringTable& strings, std::int32_t id, std::int64_t variant, std::u16string_view name);

} // namespace rct3::ui
