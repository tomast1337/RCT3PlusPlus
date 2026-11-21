// SPDX-License-Identifier: GPL-3.0-or-later
#include "video/wmv/decoder.hpp"

#include <doctest/doctest.h>

TEST_CASE("the Windows WMV decoder turns down a picture without a size")
{
    using rct3::video::wmv::Decoder;
    CHECK_FALSE(Decoder::open({ .width = 0, .height = 480 }).has_value());
    CHECK_FALSE(Decoder::open({ .width = 640, .height = 0 }).has_value());
}
