// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_effect must return an effect or an error for any input, never crash, read out of
// bounds or trip a sanitizer, and every index in an effect it returns must be in range. Run with
// `./nob fuzz particle_effect`.
#include "particles/effect.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto effect = rct3::particles::read_effect(std::as_bytes(std::span(data, size)));
    if (!effect)
        return 0;
    for (const std::size_t r : effect->roots)
        if (r >= effect->emitters.size())
            std::abort();
    for (const auto& e : effect->emitters)
        if (e.particle && *e.particle >= effect->particles.size())
            std::abort();
    for (const auto& p : effect->particles)
        for (const std::size_t e : p.emitters)
            if (e >= effect->emitters.size())
                std::abort();
    return 0;
}
