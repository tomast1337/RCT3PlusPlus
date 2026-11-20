// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/info/animal.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;
using namespace rct3::ui::screens::info;

namespace
{
    StringTable table()
    {
        StringTable s;
        s.add("Male", u"Boy");
        s.add("Adult", u"Grown");
        s.add("Female", u"Girl");
        s.add("Baby", u"Little");
        s.add("WildAnimal_Rescued", u"(saved)");
        s.add("WildAnimal_Species", u"Kind:");
        s.add("Name_Wombat", u"Wombat");
        s.add("WildAnimalState_Eat", u"Munching");
        s.add("WildAnimalStat_Hunger", u"Peckish");
        s.add("WildAnimalStat_Cleanliness", u"Tidy");
        s.add("WombatInfo_Name", u"Common wombat");
        s.add("WombatInfo_Diet", u"Grass");
        s.add("AnimalInfo_DietLabel", u"Eats:");
        s.add("AnimalInfo_House", u"Lives in a #SHELTERNAME#");
        s.add("BurrowName", u"burrow");
        return s;
    }

    const InfoLine& line(const InfoPage& page, std::string_view name)
    {
        const auto at = std::ranges::find(page.lines, name, &InfoLine::name);
        REQUIRE(at != page.lines.end());
        return *at;
    }
} // namespace

TEST_CASE("an animal's details name it, give its kind and say what it is doing")
{
    const StringTable strings = table();
    const simulation::Animal a { .species = "wombat", .name = "Digger", .adult = true, .male = true, .job = simulation::AnimalJob::Feeding, .hunger = units::Share { 0.8f } };
    const InfoPage page = animal_page(AnimalTab::Details, { .animal = a, .species_text = "name_wombat" }, strings);
    CHECK(page.title == u"Digger");
    CHECK(page.heading == u"Boy Grown");
    const simulation::Animal saved { .species = "wombat", .adult = false, .male = false, .rescued = true };
    CHECK(animal_page(AnimalTab::Details, { .animal = saved, .species_text = "name_wombat" }, strings).heading == u"Girl Little (saved)");
    CHECK(line(page, "WildAnimal_Species").value == u"Wombat");
    const auto name = std::ranges::find(page.controls, "AnimalName", &InfoControl::name);
    REQUIRE(name != page.controls.end());
    CHECK(name->text == u"Digger");
    CHECK(name->id == animal_control(AnimalControl::Name));
    const auto doing = std::ranges::find(page.controls, "AnimalDoing", &InfoControl::name);
    REQUIRE(doing != page.controls.end());
    CHECK(doing->label == u"Munching");
    CHECK(std::ranges::contains(page.controls, animal_control(AnimalControl::Locate), &InfoControl::id));
}

TEST_CASE("an animal's stats are six bars, its enclosure's dung making it less clean")
{
    const StringTable strings = table();
    const simulation::Animal a { .species = "wombat", .hunger = units::Share { 0.25f } };
    const InfoPage page = animal_page(AnimalTab::Stats, { .animal = a, .cleanliness = units::Share { 0.4f } }, strings);
    CHECK(page.lines.size() == 6);
    CHECK(std::ranges::all_of(page.lines, [](const InfoLine& l) { return l.bar.has_value(); }));
    CHECK(*line(page, "WildAnimalStat_Hunger").bar == doctest::Approx(0.25f));
    CHECK(*line(page, "WildAnimalStat_Cleanliness").bar == doctest::Approx(0.4f));
    CHECK(line(page, "WildAnimalStat_Cleanliness").value == u"40 %");
}

TEST_CASE("an animal's species page gives its kind's facts and its house")
{
    const StringTable strings = table();
    const simulation::Animal a { .species = "wombat" };
    const InfoPage page = animal_page(AnimalTab::Species, { .animal = a, .species_own_name = "Wombat", .house_text = "burrowname" }, strings);
    CHECK(page.heading == u"Common wombat");
    CHECK(line(page, "AnimalInfo_DietLabel").label == u"Eats: Grass");
    CHECK(line(page, "AnimalInfo_House").label == u"Lives in a burrow");
}
