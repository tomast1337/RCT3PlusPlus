// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_display, and read_park_displays on the same bytes read as a park, must return displays or
// an error for any input, never crash, read out of bounds or trip a sanitizer; every shot in a display they return
// must name one of its launchers, and every launcher's aim keys must be in time order. Run with
// `./nob fuzz firework_display`.
#include "particles/display.hpp"
#include "save/save_file.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) // c-api
{
    const auto bytes = std::as_bytes(std::span(data, size));
    const auto check = [](const rct3::particles::Display& display) {
        for (const auto& shot : display.shots)
            if (shot.launcher >= display.launchers.size())
                std::abort();
        for (const auto& launcher : display.launchers)
            if (!std::ranges::is_sorted(launcher.keys, { }, &rct3::particles::AimKey::seconds))
                std::abort();
    };
    if (const auto display = rct3::particles::read_display(bytes))
        check(*display);
    if (const auto park = rct3::save::read_save(bytes, { .verify_checksum = false }))
        if (const auto displays = rct3::particles::read_park_displays(*park))
            std::ranges::for_each(*displays, check);
    return 0;
}
