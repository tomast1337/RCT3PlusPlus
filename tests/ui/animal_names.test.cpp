// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/animal_names.hpp"

#include "core/types.hpp"

#include <doctest/doctest.h>
#include <set>
#include <string>

using namespace rct3;
using rct3::ovl::animal::AnimalAge;

namespace
{
    /// A language with a few invented first names and the words animals are named with.
    ui::StringTable names_language()
    {
        ui::StringTable t;
        t.add("GuestName_Male0000", u"Arnold");
        t.add("GuestName_Male0001", u"Kevin");
        t.add("GuestName_Male0002", u"Kurt");
        t.add("GuestName_Female0000", u"Kim");
        t.add("GuestName_Female0001", u"Olga");
        t.add("AnimalMiddleNameThe", u"the");
        t.add("Name_WildAnimalSpecies_Koala_ForAnimalNaming", u"Koala");
        t.add("Name_WildAnimalSpecies_Yak_ForAnimalNaming", u"Yak");
        t.add("AnimalDescendantSuffix2", u"Jr");
        t.add("AnimalDescendantSuffix3", u"III");
        return t;
    }
} // namespace

TEST_CASE("a new animal's first name starts as its species' word does, off its sex's list")
{
    const ui::StringTable t = names_language();
    auto dice = random::seeded(random::Seed { 7 });
    std::set<std::string> males;
    for (i32 i = 0; i < 40; ++i)
        males.insert(ui::new_animal_name(t, "koala", AnimalAge::YoungMale, dice));
    CHECK(males == std::set<std::string> { "Kevin the Koala", "Kurt the Koala" });
    CHECK(ui::new_animal_name(t, "KOALA", AnimalAge::AdultFemale, dice) == "Kim the Koala");
}

TEST_CASE("with no first name of its species' letter, any of its sex's names will do")
{
    const ui::StringTable t = names_language();
    auto dice = random::seeded(random::Seed { 3 });
    std::set<std::string> females;
    for (i32 i = 0; i < 40; ++i)
        females.insert(ui::new_animal_name(t, "yak", AnimalAge::AdultFemale, dice));
    CHECK(females == std::set<std::string> { "Kim the Yak", "Olga the Yak" });
}

TEST_CASE("a saved animal's name is its pattern with its first names, species and generation put in")
{
    const ui::StringTable t = names_language();
    save::model::ZooAnimal a { .name_template = "%NAME% %SPECIES% %SUFFIX%", .first_names = "Kurt the", .generation_suffix = 1, .species = "koala" };
    CHECK(ui::saved_animal_name(t, a) == "Kurt the Koala");
    a.generation_suffix = 2;
    CHECK(ui::saved_animal_name(t, a) == "Kurt the Koala Jr");
    a.generation_suffix = 3;
    CHECK(ui::saved_animal_name(t, a) == "Kurt the Koala III");
    // No pattern kept: the first names and the species.
    a.name_template.clear();
    CHECK(ui::saved_animal_name(t, a) == "Kurt the Koala");
}
