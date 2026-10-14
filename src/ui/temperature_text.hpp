// SPDX-License-Identifier: GPL-3.0-or-later
//
// A temperature as the park shows it: whole degrees, rounded down, in Celsius or Fahrenheit as the player
// picked in the options. Saves keep it in degrees Celsius.
#pragma once

#include <string>

namespace rct3::ui
{

    [[nodiscard]] std::u16string temperature_text(float celsius, bool fahrenheit);

} // namespace rct3::ui
