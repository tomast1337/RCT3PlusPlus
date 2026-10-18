// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_saved_park must return a park or an error for any input, never crash, read out of
// bounds or trip a sanitizer, and a park it returns must write a file that reads again. Run with
// `./nob fuzz saved_park`.
#include "parkfile/saved_park.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    if (const auto park = rct3::parkfile::read_saved_park(bytes); park && !rct3::parkfile::read_saved_park(rct3::parkfile::write_saved_park(*park)))
        __builtin_trap();
    return 0;
}
