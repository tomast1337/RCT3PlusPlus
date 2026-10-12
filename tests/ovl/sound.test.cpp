// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/sound.hpp"

#include "builder.hpp"

#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_sounds;

namespace
{

    void append_f32(std::vector<std::byte>& out, float v)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        append_u32(out, bits);
    }

    // An 80-byte descriptor. Words: format/channels, rate, bytes per second, align/bits, 2 zero, 9 floats,
    // flag, data address, data size, 2 zero.
    std::vector<std::byte> descriptor(std::uint32_t rate, std::uint32_t data_address, std::uint32_t data_size, std::uint32_t flag = 0)
    {
        std::vector<std::byte> out;
        append_u16(out, 1);
        append_u16(out, 1);
        append_u32(out, rate);
        append_u32(out, rate * 2);
        append_u16(out, 2);
        append_u16(out, 16);
        append_u32(out, 0);
        append_u32(out, 0);
        for (int i = 0; i < 9; ++i)
            append_f32(out, 0.5f * static_cast<float>(i + 1));
        append_u32(out, flag);
        append_u32(out, data_address);
        append_u32(out, data_size);
        append_u32(out, 0);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> row(std::uint32_t name, std::uint32_t data)
    {
        std::vector<std::byte> out;
        append_u32(out, name);
        append_u32(out, data);
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

} // namespace

TEST_CASE("sound items expose format, parameters and samples")
{
    // Block type 0: name "Bell:snd" (9 bytes) then 8 sample bytes at address 9.
    std::vector<std::byte> strings = bytes_of_string(std::string("Bell:snd\0", 9));
    const auto samples = bytes_of({ 1, 2, 3, 4, 5, 6, 7, 8 });
    strings.insert(strings.end(), samples.begin(), samples.end());
    // Block type 1 starts at address 17 and holds the descriptor.
    const auto info = descriptor(22050, 9, 8, 1);

    auto overlay = Overlay::from_bytes(build_file(5, { { strings }, { info }, { row(0, 17) } }, { }), std::nullopt);
    REQUIRE(overlay.has_value());

    const auto items = read_sounds(*overlay, overlay->common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "Bell");
    REQUIRE(items[0].info.has_value());

    const auto& s = *items[0].info;
    CHECK(s.format_tag == 1);
    CHECK(s.channels == 1);
    CHECK(s.sample_rate == 22050);
    CHECK(s.bytes_per_second == 44100);
    CHECK(s.block_align == 2);
    CHECK(s.bits_per_sample == 16);
    CHECK(s.parameters[0] == 0.5f);
    CHECK(s.parameters[8] == 4.5f);
    CHECK(s.flag == 1);
    CHECK(s.data_address == 9);
    CHECK(s.data_size == 8);
    REQUIRE(s.samples.has_value());
    REQUIRE(s.samples->size() == 8);
    CHECK((*s.samples)[0] == std::byte { 1 });
    CHECK((*s.samples)[7] == std::byte { 8 });
}

TEST_CASE("sound item edge cases")
{
    std::vector<std::byte> strings = bytes_of_string(std::string("A:snd\0B:snd\0", 12));

    SUBCASE("samples that leave their block are reported as missing")
    {
        // Address 0 holds 12 bytes in block 0, so a size of 13 does not fit.
        const auto info = descriptor(16000, 0, 13);
        auto overlay = Overlay::from_bytes(build_file(5, { { strings }, { info }, { row(0, 12) } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        const auto items = read_sounds(*overlay, overlay->common());
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].info.has_value());
        CHECK_FALSE(items[0].info->samples.has_value());
        CHECK(items[0].info->sample_rate == 16000);
    }

    SUBCASE("a descriptor shorter than 80 bytes is unreadable")
    {
        auto info = descriptor(16000, 0, 4);
        info.resize(60);
        auto overlay = Overlay::from_bytes(build_file(5, { { strings }, { info }, { row(0, 12) } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        const auto items = read_sounds(*overlay, overlay->common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].info.has_value());
    }

    SUBCASE("other tags are ignored")
    {
        const std::vector<std::byte> other = bytes_of_string(std::string("A:txt\0", 6));
        auto overlay = Overlay::from_bytes(build_file(5, { { other }, { }, { row(0, 0) } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        CHECK(read_sounds(*overlay, overlay->common()).empty());
    }
}
