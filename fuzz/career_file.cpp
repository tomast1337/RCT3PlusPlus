// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_career must return a career or an error for any input, and write back what it
// read, never crash, read out of bounds or trip a sanitizer. Run with `./nob fuzz career_file`.
#include "career/career_file.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto career = rct3::career::read_career(std::as_bytes(std::span(data, size)));
    if (career)
        (void)rct3::career::write_career(*career);
    return 0;
}
