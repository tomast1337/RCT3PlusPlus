// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: parse_translation must return strings or an error for any text, never crash or trip a
// sanitizer. Run with `./nob fuzz translation_file`.
#include "ui/translation_file.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    (void)rct3::ui::parse_translation(std::string_view(reinterpret_cast<const char*>(data), size));
    return 0;
}
