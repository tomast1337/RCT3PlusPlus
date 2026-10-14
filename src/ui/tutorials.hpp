// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the game's text says about each game's tutorials. Their keys start "Tutorial" for the original
// game and "Tutorial_AOP<k>_" for expansion k; tutorial n (from 1, up to the first number missing) has its
// name and description ("<start>List_Name<n>", "<start>List_Description<n>") and the skin of its round
// picture ("<start>Icon<n>"). Its park is "Tutorials/<start><n>.dat" in the game's folder.
//
// Two of the original game's tutorials depend on the expansions: its last one, the extras, is left out
// once the second expansion is installed, and comes from another park ("_Alternate" after the number)
// once the first one is.
#pragma once

#include "ui/string_table.hpp"

#include <span>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct TutorialInfo
    {
        std::u16string name;
        std::u16string description;
        std::string icon;
        std::string park; ///< Relative to the game's folder
    };

    /// Game `pack`'s tutorials, in order: 0 the original game, then each expansion. `expansions` says
    /// which expansions are installed, by number.
    [[nodiscard]] std::vector<TutorialInfo> tutorials(const StringTable& strings, int pack, std::span<const bool> expansions);

} // namespace rct3::ui
