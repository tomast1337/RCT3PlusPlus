// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/flexi_texture.hpp"

#include "builder.hpp"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_flexi_textures;

namespace
{

    // Symbol name block: "T:ftx" and NUL, 6 bytes. The structure block follows it at address 6.
    constexpr std::uint32_t kBase = 6;

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    std::vector<std::byte> row(std::uint32_t name, std::uint32_t data)
    {
        std::vector<std::byte> out;
        words(out, { name, data });
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    // Layout inside the structure block (offsets, then +kBase for addresses):
    //   0 header(36) | 36 sequence(2*4) | 44 frames(2*28) | 100 palette0(1024) | 1124 pixels0(16) |
    //   1140 alpha0(16) | 1156 palette1(1024) | 2180 pixels1(16) | 2196 end
    std::vector<std::byte> structure(std::uint32_t second_frame_pixels = kBase + 2180)
    {
        std::vector<std::byte> out;
        words(out, { 2, 4, 4, 9, 3, 2, kBase + 36, 2, kBase + 44 });
        words(out, { 1, 0 });
        words(out, { 2, 4, 4, 3, kBase + 100, kBase + 1124, kBase + 1140 });
        words(out, { 2, 4, 4, 3, kBase + 1156, second_frame_pixels, 0 });
        for (std::size_t i = 0; i < 1024; ++i)
            out.push_back(static_cast<std::byte>(i & 0xFF));
        for (std::size_t i = 0; i < 16; ++i)
            out.push_back(static_cast<std::byte>(0x10 + i));
        for (std::size_t i = 0; i < 16; ++i)
            out.push_back(static_cast<std::byte>(0x80 + i));
        for (std::size_t i = 0; i < 1024; ++i)
            out.push_back(static_cast<std::byte>(0xFF - (i & 0xFF)));
        for (std::size_t i = 0; i < 16; ++i)
            out.push_back(static_cast<std::byte>(0x40 + i));
        return out;
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { bytes_of_string(std::string("T:ftx\0", 6)) }, { block }, { row(0, kBase) } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("flexi textures expose frames, sequence and planes")
{
    const auto overlay = make(structure());
    const auto items = read_flexi_textures(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "T");
    REQUIRE(items[0].texture.has_value());

    const auto& t = *items[0].texture;
    CHECK(t.log2_size == 2);
    CHECK(t.width == 4);
    CHECK(t.height == 4);
    CHECK(t.animation == 9);
    CHECK(t.flags == 3);
    REQUIRE(t.sequence.size() == 2);
    CHECK(t.sequence[0] == 1);
    CHECK(t.sequence[1] == 0);

    REQUIRE(t.frames.size() == 2);
    const auto& f0 = t.frames[0];
    CHECK(f0.width == 4);
    CHECK(f0.flags == 3);
    REQUIRE(f0.palette.size() == 1024);
    CHECK(f0.palette[5] == std::byte { 5 });
    REQUIRE(f0.pixels.size() == 16);
    CHECK(f0.pixels[0] == std::byte { 0x10 });
    REQUIRE(f0.alpha.has_value());
    REQUIRE(f0.alpha->size() == 16);
    CHECK((*f0.alpha)[15] == std::byte { 0x8F });

    const auto& f1 = t.frames[1];
    CHECK(f1.palette[0] == std::byte { 0xFF });
    CHECK(f1.pixels[0] == std::byte { 0x40 });
    CHECK_FALSE(f1.alpha.has_value());
}

TEST_CASE("flexi texture edge cases")
{
    SUBCASE("a pixel plane that leaves its block makes the texture unreadable")
    {
        // Point the second frame's pixels at the last 8 bytes of the block: 16 bytes do not fit.
        const auto overlay = make(structure(kBase + 2196 - 8));
        const auto items = read_flexi_textures(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].texture.has_value());
    }

    SUBCASE("a sequence index past the frame count makes the texture unreadable")
    {
        auto block = structure();
        block[36] = std::byte { 2 }; // first sequence entry = 2, but only frames 0 and 1 exist
        const auto overlay = make(block);
        const auto items = read_flexi_textures(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].texture.has_value());
    }

    SUBCASE("a header shorter than 36 bytes is unreadable")
    {
        auto block = structure();
        block.resize(20);
        const auto overlay = make(block);
        const auto items = read_flexi_textures(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].texture.has_value());
    }

    SUBCASE("a frame count that cannot fit is refused without allocating")
    {
        auto block = structure();
        block[28] = std::byte { 0xFF };
        block[29] = std::byte { 0xFF };
        block[30] = std::byte { 0xFF };
        block[31] = std::byte { 0x7F };
        const auto overlay = make(block);
        const auto items = read_flexi_textures(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].texture.has_value());
    }
}
