// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/temperature_text.hpp"

#include "strings/unicode.hpp"

#include <cmath>
#include <format>

namespace rct3::ui
{

    std::u16string temperature_text(float celsius, bool fahrenheit)
    {
        const float degrees = fahrenheit ? celsius * 9 / 5 + 32 : celsius;
        return strings::to_utf16(std::format("{}°{}", static_cast<long long>(std::floor(degrees)), fahrenheit ? 'F' : 'C'));
    }

} // namespace rct3::ui
