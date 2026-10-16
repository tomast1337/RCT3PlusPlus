// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_park_file must return a world or an error for any input, never crash, read out of
// bounds or trip a sanitizer, and a world it returns must write a file that reads again. Run with
// `./nob fuzz park_file`.
#include "parkfile/park_file.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    const auto world = rct3::parkfile::read_park_file(bytes);
    if (world)
    {
        const auto again = rct3::parkfile::read_park_file(rct3::parkfile::write_park_file(*world));
        if (!again)
            __builtin_trap();
    }
    return 0;
}
