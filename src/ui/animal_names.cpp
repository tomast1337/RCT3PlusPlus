// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/animal_names.hpp"

#include "core/types.hpp"
#include "strings/unicode.hpp"

#include <format>
#include <vector>

namespace rct3::ui
{

    namespace
    {
        /// What a park file's name pattern puts each part in place of.
        constexpr std::string_view kFirstNamesField = "%NAME%";
        constexpr std::string_view kSpeciesField = "%SPECIES%";
        constexpr std::string_view kSuffixField = "%SUFFIX%";
        /// The generation that has no suffix: an animal not born in the park.
        constexpr i32 kFirstGeneration = 1;
    } // namespace

    std::u16string species_word(const StringTable& strings, std::string_view species)
    {
        return std::u16string(strings.get_ignoring_case(std::format("Name_WildAnimalSpecies_{}_ForAnimalNaming", species)));
    }

    std::string saved_animal_name(const StringTable& strings, const save::model::ZooAnimal& animal)
    {
        std::string name = animal.name_template.empty() ? std::format("{} {}", kFirstNamesField, kSpeciesField) : animal.name_template;
        const auto put = [&](std::string_view field, std::string_view value) {
            if (const auto at = name.find(field); at != std::string::npos)
                name.replace(at, field.size(), value);
        };
        put(kFirstNamesField, animal.first_names);
        put(kSpeciesField, strings::to_utf8(species_word(strings, animal.species)));
        put(kSuffixField, animal.generation_suffix > kFirstGeneration ? strings::to_utf8(strings.get(std::format("AnimalDescendantSuffix{}", animal.generation_suffix))) : std::string { });
        // A part left empty leaves its spaces behind.
        for (auto twice = name.find("  "); twice != std::string::npos; twice = name.find("  "))
            name.erase(twice, 1);
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        while (!name.empty() && name.front() == ' ')
            name.erase(0, 1);
        return name;
    }

    std::string new_animal_name(const StringTable& strings, std::string_view species, ovl::animal::AnimalAge age, random::RandomState& dice)
    {
        const bool male = age == ovl::animal::AnimalAge::AdultMale || age == ovl::animal::AnimalAge::YoungMale;
        const std::string_view list = male ? "GuestName_Male" : "GuestName_Female";
        const std::u16string word = species_word(strings, species);
        const std::u16string initial = strings::search_folded(word).substr(0, 1);
        std::vector<std::u16string_view> names;
        std::vector<std::u16string_view> alike;
        for (const std::string& key : strings.keys())
        {
            if (!key.starts_with(list))
                continue;
            const std::u16string_view first = strings.get(key);
            if (first.empty())
                continue;
            names.push_back(first);
            if (!initial.empty() && strings::search_folded(first).starts_with(initial))
                alike.push_back(first);
        }
        const std::vector<std::u16string_view>& from = alike.empty() ? names : alike;
        if (from.empty())
            return strings::to_utf8(word);
        std::u16string name(from[random::below(dice, from.size())]);
        for (const std::u16string_view part : { strings.get("AnimalMiddleNameThe"), std::u16string_view(word) })
            if (!part.empty())
                name += u' ' + std::u16string(part);
        return strings::to_utf8(name);
    }

} // namespace rct3::ui
