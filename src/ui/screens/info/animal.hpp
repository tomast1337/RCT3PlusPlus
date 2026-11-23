// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about a zoo animal out of its crate: its details (its name to change, its species,
// sex and age, what it is doing, a button that turns the view to it, one that enters AnimalCam on it, one that sets it free and one that sells it), bars of how it fares, the facts of its
// species, what guests thought of it and who adopted it.
#pragma once

#include "core/types.hpp"
#include "simulation/animals.hpp"
#include "ui/screens/info/panel.hpp"
#include "ui/string_table.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rct3::ui::screens::info
{

    enum class AnimalTab : u8
    {
        Details,
        Stats,
        Thoughts,
        Adoptions,
        Species,
    };

    struct AnimalTabInfo
    {
        AnimalTab tab = AnimalTab::Details;
        i32 slot = 0;          ///< Its place on the ring
        std::string_view icon; ///< Skin
        std::string_view help; ///< String key
    };

    /// The animal's tabs, in the order of their slots.
    inline constexpr std::array<AnimalTabInfo, 5> kAnimalTabs { {
        { AnimalTab::Details, 0, "Guest_Options", "WildAnimalGUI_NameWindow" },
        { AnimalTab::Stats, 1, "AnimalStatsButtonIcon", "WildAnimalGUI_StatsWindow" },
        { AnimalTab::Thoughts, 2, "Guest_Thoughts", "WildAnimalGUI_ThoughtsWindow" },
        { AnimalTab::Adoptions, 3, "AnimalAdoptionIcon", "WildAnimalGUI_AdoptionWindow" },
        { AnimalTab::Species, 4, "AnimalInfoButtonIcon", "WildAnimalGUI_InfoWindow" },
    } };

    /// Ids of the controls on an animal's pages (InfoControl::id).
    enum class AnimalControl : u8
    {
        Name,
        Locate,  ///< Turns the park's view to the animal
        Camera,  ///< Enters AnimalCam on it
        Release, ///< Asks whether to set it free in the wild
        Sell,    ///< Asks whether to sell it
    };

    [[nodiscard]] constexpr i32 animal_control(AnimalControl control)
    {
        return static_cast<i32>(control);
    }

    /// The animal a page is written about, how clean its enclosure is, and what the species data says of its kind.
    struct AnimalFacts
    {
        std::optional<const simulation::Animal&> animal { };
        units::Share cleanliness { 1 };    ///< Of its enclosure, by the dung in it against what its animals put up with
        units::Share habitat { 1 };        ///< Its enclosure for it, room and plants; none while out of every enclosure
        i32 space = 0;                     ///< Tiles of enclosure each animal of its kind wants; 0 when not known
        std::string_view species_text { }; ///< String key of its species' name
        /// The species' own name as its data spells it, which the string keys of its facts start with.
        std::string_view species_own_name { };
        f32 rarity = 0;                  ///< From 0, common, to 1, endangered
        std::string_view house_text { }; ///< String key of the name of the animal house its kind needs
    };

    [[nodiscard]] InfoPage animal_page(AnimalTab tab, const AnimalFacts& facts, const StringTable& strings);

} // namespace rct3::ui::screens::info
