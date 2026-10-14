// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: parse_state and parse_seed must return a value or an error for any text, never crash
// or trip a sanitizer, and what they read must write back the same. Run with `./nob fuzz random_state`.
#include "random/generator.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    if (const auto state = rct3::random::parse_state(text))
    {
        if (rct3::random::parse_state(rct3::random::to_text(*state)) != *state)
            std::abort();
    }
    if (const auto seed = rct3::random::parse_seed(text))
    {
        if (rct3::random::parse_seed(rct3::random::seed_text(*seed)) != *seed)
            std::abort();
    }
    return 0;
}
