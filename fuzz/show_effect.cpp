// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: the laser, water jet and laser drawing readers must return an effect or an error for any
// input, never crash, read out of bounds or trip a sanitizer, and every curve they return must have a value for
// each of its times. Run with `./nob fuzz show_effect`.
#include "particles/show_effect.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace
{
    void check(const rct3::particles::Curve& c)
    {
        if (c.times.size() != c.values.size())
            std::abort();
    }
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) // c-api
{
    const auto bytes = std::as_bytes(std::span(data, size));
    if (const auto laser = rct3::particles::read_laser(bytes))
    {
        for (const auto& c : laser->colour)
            check(c);
        check(laser->elevation);
        check(laser->spin);
        check(laser->width);
    }
    if (const auto jet = rct3::particles::read_water_jet(bytes))
    {
        check(jet->pressure);
        check(jet->elevation);
        check(jet->widening);
    }
    (void)rct3::particles::read_laser_drawing(bytes);
    return 0;
}
