// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/particle_skin.hpp"

#include "builder.hpp"
#include "core/types.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_particle_skins;

namespace
{

    // Name block, 16 bytes: "P:psi" 0, "Img:tex" 6. Structure block at address 16.
    constexpr u32 kBase = 16;
    constexpr u32 kNameTexture = 6;

    // Offsets inside the structure block: item (28 bytes), frame list (2 pointers), two frames of 16 bytes.
    constexpr u32 kList = 28;
    constexpr u32 kFrames = 36;
    constexpr u32 kEnd = 68;

    void words(std::vector<std::byte>& out, std::initializer_list<u32> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    u32 bits(f32 f)
    {
        return std::bit_cast<u32>(f);
    }

    std::vector<std::byte> structure(u32 count, u32 blended, u32 single)
    {
        std::vector<std::byte> out;
        words(out, { count, kBase + kList, 0, blended, single, bits(0.5f), 0 });
        words(out, { kBase + kFrames, kBase + kFrames + 16 });
        words(out, { bits(0), bits(0), bits(0.125f), bits(0.125f) });
        words(out, { bits(0.125f), bits(0), bits(0.25f), bits(0.125f) });
        REQUIRE(out.size() == kEnd);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        std::string text("P:psi\0Img:tex\0", 14);
        text.resize(kBase, '\0');
        std::vector<std::byte> symbol;
        words(symbol, { 0, kBase });
        append_u16(symbol, 1);
        append_u16(symbol, 0xFFFF);
        append_u32(symbol, 0);
        std::vector<std::byte> reference;
        words(reference, { kBase + 8, kNameTexture, 0, 0 });
        auto overlay = Overlay::from_bytes(build_file(5, { { bytes_of_string(text) }, { block }, { symbol, { }, reference } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("a particle skin reads its frames, texture, blending and timing")
{
    const auto overlay = make(structure(2, 1, 0));
    const auto items = read_particle_skins(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "P");
    REQUIRE(items[0].skin.has_value());
    const auto& s = *items[0].skin;
    CHECK(s.texture == "Img");
    CHECK(s.blended);
    CHECK(s.animated);
    CHECK(s.loop_time == 0.5f);
    REQUIRE(s.frames.size() == 2);
    CHECK(s.frames[1][0] == 0.125f);
    CHECK(s.frames[1][2] == 0.25f);
}

TEST_CASE("a particle skin with frames outside the file is unreadable")
{
    const auto single = make(structure(1, 0, 1));
    const auto items = read_particle_skins(single, single.common());
    REQUIRE(items[0].skin.has_value());
    CHECK_FALSE(items[0].skin->blended);
    CHECK_FALSE(items[0].skin->animated);

    const auto overlay = make(structure(9, 0, 0)); // The list runs past the block
    CHECK_FALSE(read_particle_skins(overlay, overlay.common())[0].skin.has_value());
}
