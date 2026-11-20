// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zoo animals' names, made from the language's strings: a first name starting with the same letter as the word
// its species is named with, the language's word for "the", and the species' word ("Zebedee the Zebra"). The first
// names are the guests' lists, the male one for a male and the female one for a female. A park file keeps an
// animal's name as a pattern with its first names, the species' word and the suffix of its generation to put in
// ("Jr" for the second, then roman numbers).
#pragma once

#include "core/types.hpp"
#include "ovl/animal/species.hpp"
#include "random/generator.hpp"
#include "save/model/people.hpp"
#include "ui/string_table.hpp"

#include <string>
#include <string_view>

namespace rct3::ui
{

    /// The word the animals of `species` (its symbol, in any case) are named with; empty when the language has none.
    [[nodiscard]] std::u16string species_word(const StringTable& strings, std::string_view species);

    /// The name of an animal a park file kept: its pattern with its first names, its species' word and its
    /// generation's suffix put in.
    [[nodiscard]] std::string saved_animal_name(const StringTable& strings, const save::model::ZooAnimal& animal);

    /// A name for a new animal of `species`, of the sex `age` says: a first name off that sex's list, picked with
    /// `dice` among those starting with the species word's first letter (among them all when none does), then
    /// "the" and the word.
    [[nodiscard]] std::string new_animal_name(const StringTable& strings, std::string_view species, ovl::animal::AnimalAge age, random::RandomState& dice);

} // namespace rct3::ui
