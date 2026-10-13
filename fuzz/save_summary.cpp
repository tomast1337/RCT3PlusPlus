// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_summary must return a summary or an error for any input, never crash, read out of
// bounds or trip a sanitizer. Run with `./nob fuzz save_summary`.
#include "save/summary.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto summary = rct3::save::read_summary(std::as_bytes(std::span(data, size)));
    if (!summary)
        return 0;
    // A picture is whole or absent.
    if (!summary->preview.empty() && summary->preview.size() != std::size_t { rct3::save::kPreviewWidth } * rct3::save::kPreviewHeight * 4)
        __builtin_trap();
    return 0;
}
