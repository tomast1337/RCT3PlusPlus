// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/info/animal.hpp"

#include "core/types.hpp"
#include "strings/unicode.hpp"
#include "ui/screens/build/rows.hpp"
#include "ui/screens/info/attraction.hpp"
#include "ui/screens/info/person.hpp"
#include "units/units.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace rct3::ui::screens::info
{

    namespace
    {
        constexpr f32 kWidePageWidth = 440; ///< Chosen by eye, as a ride's maintenance page: room for a bar and its percent, or a fact

        std::u16string text(const StringTable& strings, std::string_view key)
        {
            return std::u16string(strings.get(key));
        }

        /// The text of `key` with `field` in it put as `value`.
        std::u16string filled(const StringTable& strings, std::string_view key, std::string_view field, const std::u16string& value)
        {
            std::u16string out = text(strings, key);
            if (const auto at = out.find(strings::to_utf16(field)); at != std::u16string::npos)
                out.replace(at, field.size(), value);
            return out;
        }

        InfoLine bar(const StringTable& strings, std::string_view label, units::Share share)
        {
            const f32 fraction = std::clamp(share.value, 0.0f, 1.0f);
            const std::string percent = std::format("{} %", std::lround(fraction * units::kPercent));
            return { .label = text(strings, label), .value = strings::to_utf16(percent), .half = false, .name = std::string(label), .bar = fraction };
        }

        /// What the animal is doing, as the game's states call it.
        std::string_view doing(const simulation::Animal& a)
        {
            using simulation::AnimalJob;
            if (a.escaped)
                return "WildAnimalState_Escaped";
            switch (a.job)
            {
                case AnimalJob::Crated:
                    return "WildAnimalState_Crate";
                case AnimalJob::Idle:
                    return "WildAnimalState_Decision";
                case AnimalJob::Strolling:
                    return "WildAnimalState_Move";
                case AnimalJob::Feeding:
                    return a.hunger.value >= a.thirst.value ? "WildAnimalState_Eat" : "WildAnimalState_Drink";
                case AnimalJob::Sleeping:
                    return "WildAnimalState_Sleep";
            }
            return { };
        }

        void details(InfoPage& page, const AnimalFacts& f, const simulation::Animal& a, const StringTable& strings)
        {
            page.heading = text(strings, a.male ? "Male" : "Female") + u' ' + text(strings, a.adult ? "Adult" : "Baby");
            if (a.rescued)
                page.heading += u' ' + text(strings, "WildAnimal_Rescued");
            page.lines.push_back({ .label = text(strings, "WildAnimal_Species"), .value = std::u16string(strings.get_ignoring_case(f.species_text)), .name = "WildAnimal_Species" });
            page.controls.push_back({ .id = animal_control(AnimalControl::Name), .name = "AnimalName", .kind = InfoControlKind::Text, .maximum = kLongestName, .text = strings::to_utf16(a.name), .width = kNameWidth });
            page.controls.push_back({ .name = "AnimalDoing", .kind = InfoControlKind::Words, .label = text(strings, doing(a)) });
            page.controls.push_back({ .id = animal_control(AnimalControl::Locate), .name = "CenterInViewIcon", .kind = InfoControlKind::Button, .icon = "CenterInViewIcon", .help = text(strings, "TT_AttractionDialogIconFocusCameraButton") });
            page.controls.push_back({ .id = animal_control(AnimalControl::Camera), .name = "AnimalCam", .kind = InfoControlKind::Button, .icon = "RideCamGUI_Enabled", .beside = true, .help = text(strings, "AnimalCamButtonTT") });
        }

        void stats(InfoPage& page, const AnimalFacts& f, const simulation::Animal& a, const StringTable& strings)
        {
            page.width = kWidePageWidth;
            page.lines.push_back(bar(strings, "WildAnimalStat_Health", a.health));
            page.lines.push_back(bar(strings, "WildAnimalStat_Enclosure", units::Share { a.escaped ? 0.0f : f.habitat.value }));
            page.lines.push_back(bar(strings, "WildAnimalStat_Hunger", a.hunger));
            page.lines.push_back(bar(strings, "WildAnimalStat_Thirst", a.thirst));
            page.lines.push_back(bar(strings, "WildAnimalStat_Social", units::Share { 1 - a.loneliness.value }));
            page.lines.push_back(bar(strings, "WildAnimalStat_Cleanliness", f.cleanliness));
        }

        void species(InfoPage& page, const AnimalFacts& f, const StringTable& strings)
        {
            if (f.species_own_name.empty())
                return;
            // The data spells the species' own name in any case.
            const auto species_text = [&](std::string key) { return std::u16string(strings.get_ignoring_case(key)); };
            page.heading = species_text(std::format("{}Info_Name", f.species_own_name));
            page.width = kWidePageWidth;
            // A fact is a sentence, longer than the value column holds: it follows its label on one line.
            const auto fact = [&](std::string_view label, std::string key) { page.lines.push_back({ .label = text(strings, label) + u' ' + species_text(std::move(key)), .value = { }, .name = std::string(label) }); };
            fact("AnimalInfo_HabitatLabel", std::format("{}Info_Habitat", f.species_own_name));
            fact("AnimalInfo_DietLabel", std::format("{}Info_Diet", f.species_own_name));
            fact("AnimalInfo_SizeLabel", std::format("{}Info_Size", f.species_own_name));
            fact("AnimalInfo_WeightLabel", std::format("{}Info_Weight", f.species_own_name));
            page.lines.push_back({ .label = text(strings, "AnimalInfo_Rarity"), .value = text(strings, build::rarity_text(f.rarity)), .name = "AnimalInfo_Rarity" });
            if (f.space > 0)
                page.lines.push_back({ .label = filled(strings, "AnimalInfo_MinSpace", "#MINSPACE#", strings::to_utf16(std::to_string(f.space))), .value = { }, .name = "AnimalInfo_MinSpace" });
            if (!f.house_text.empty())
                page.lines.push_back({ .label = filled(strings, "AnimalInfo_House", "#SHELTERNAME#", std::u16string(strings.get_ignoring_case(f.house_text))), .value = { }, .name = "AnimalInfo_House" });
        }

    } // namespace

    InfoPage animal_page(AnimalTab tab, const AnimalFacts& facts, const StringTable& strings)
    {
        InfoPage page;
        if (!facts.animal)
            return page;
        const simulation::Animal& a = *facts.animal;
        page.title = strings::to_utf16(a.name);
        switch (tab)
        {
            case AnimalTab::Details:
                details(page, facts, a, strings);
                break;
            case AnimalTab::Stats:
                stats(page, facts, a, strings);
                break;
            case AnimalTab::Thoughts:
                // shortcut: guests do not think about animals yet; list their thoughts once they do.
                page.note = text(strings, "Rct3pp_AnimalNoThoughts");
                break;
            case AnimalTab::Adoptions:
                // shortcut: nobody adopts animals yet; list the adopters and the fee once guests do.
                page.lines.push_back({ .label = text(strings, "WildAnimalAdoptersLabel"), .value = u"0", .name = "WildAnimalAdoptersLabel" });
                break;
            case AnimalTab::Species:
                species(page, facts, strings);
                break;
        }
        return page;
    }

} // namespace rct3::ui::screens::info
