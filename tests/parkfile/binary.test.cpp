// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/binary.hpp"

#include "core/types.hpp"

#include <cstddef>
#include <doctest/doctest.h>
#include <vector>

using namespace rct3;

TEST_CASE("a park file's numbers are little-endian whatever the machine's own order")
{
    // Written and read the same on every machine, so a park saved on one plays the same on another.
    parkfile::detail::Writer w;
    w.number(u16 { 0x0102 });
    w.number(u32 { 0x0102'0304 });
    w.number(i64 { -2 });
    w.number(1.5f);
    w.number(-2.0);
    const std::vector<std::byte> bytes = w.take();
    std::vector<u8> numbers;
    for (const std::byte b : bytes)
        numbers.push_back(std::to_integer<u8>(b));
    CHECK(
        numbers
        == std::vector<u8> {
            0x02, 0x01,                                     // u16
            0x04, 0x03, 0x02, 0x01,                         // u32
            0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // i64 -2
            0x00, 0x00, 0xc0, 0x3f,                         // f32 1.5
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, // f64 -2
        });
    parkfile::detail::Reader r(bytes);
    CHECK(r.number<u16>() == 0x0102);
    CHECK(r.number<u32>() == 0x0102'0304u);
    CHECK(r.number<i64>() == -2);
    CHECK(r.number<f32>() == 1.5f);
    CHECK(r.number<f64>() == -2.0);
    CHECK(r.ok());
}
