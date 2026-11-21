// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/wma/decoder.hpp"

#include <doctest/doctest.h>

TEST_CASE("the Windows WMA decoder turns down a format it cannot take")
{
    using rct3::audio::wma::Decoder;
    CHECK_FALSE(Decoder::open({ .version = 2, .sample_rate = 44100, .channels = 0, .bit_rate = 64000, .block_align = 1487 }).has_value());
    CHECK_FALSE(Decoder::open({ .version = 3, .sample_rate = 44100, .channels = 2, .bit_rate = 64000, .block_align = 1487 }).has_value());
}
