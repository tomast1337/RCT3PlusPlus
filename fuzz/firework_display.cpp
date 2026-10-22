// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_display must return a display or an error for any input, never crash, read out of
// bounds or trip a sanitizer, and every shot in a display it returns must name one of its launchers. Run with
// `./nob fuzz firework_display`.
#include "particles/display.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto display = rct3::particles::read_display(std::as_bytes(std::span(data, size)));
    if (!display)
        return 0;
    for (const auto& shot : display->shots)
        if (shot.launcher >= display->launchers.size())
            std::abort();
    return 0;
}
