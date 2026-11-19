// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/temperature_text.hpp"

#include "core/types.hpp"
#include "strings/unicode.hpp"

#include <cmath>
#include <cstdint>
#include <format>

namespace rct3::ui
{

    std::u16string temperature_text(f32 celsius, bool fahrenheit)
    {
        const f32 degrees = fahrenheit ? celsius * 9 / 5 + 32 : celsius;
        return strings::to_utf16(std::format("{}°{}", static_cast<i64>(std::floor(degrees)), fahrenheit ? 'F' : 'C'));
    }

} // namespace rct3::ui
