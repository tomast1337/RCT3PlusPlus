// SPDX-License-Identifier: GPL-3.0-or-later
//
// The script of a tutorial park: one Tutorial object naming the tutorial and listing its steps. Each step
// has a title and text in English, where the window sits and how wide it is, and the triggers that end it;
// a step without triggers waits for the player to continue. The game shows the language's own text where
// it has one, under keys built from the tutorial's name and the step's number.
//
// The model keeps what showing the steps needs. Trigger types are kept as their names, as their meaning is
// not known yet.
#pragma once

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace rct3::save
{

    struct TutorialStep
    {
        std::string title;
        std::string text;
        std::vector<std::string> triggers; ///< Names of what ends the step; none waits for the player
        std::int32_t window_position = 0;  ///< Where its window sits, as stored
        std::int32_t window_width = 0;     ///< In interface units, 0 when not stored
    };

    struct TutorialScript
    {
        std::string name; ///< Builds the keys of the steps' text in the language
        std::string title;
        std::vector<TutorialStep> steps;
    };

    /// The tutorial in a decoded Tutorial object.
    [[nodiscard]] TutorialScript tutorial_from(const DecodedObject& object);

    /// The save's tutorial, if it has one: the first Tutorial object.
    [[nodiscard]] std::expected<std::optional<TutorialScript>, Error> load_tutorial(const SaveFile& file);

} // namespace rct3::save
