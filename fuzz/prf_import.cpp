// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_prf must return a career or an error for any input, never crash, read out of
// bounds or trip a sanitizer. Run with `./nob fuzz prf_import`.
#include "career/prf_import.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto prf = rct3::career::read_prf(std::as_bytes(std::span(data, size)));
    if (prf)
    {
        const std::vector<std::string> ids { "A", "B", "C" };
        (void)rct3::career::career_from_prf(*prf, ids);
    }
    return 0;
}
