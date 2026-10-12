// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/simple_anim.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_simple_anims;

namespace
{

    // Name block: "A:san" at 0, padded to 16. The structure block follows at address 16.
    constexpr std::uint32_t kBase = 16;
    constexpr std::uint32_t kTimes = 16;   // 3 keys * 4
    constexpr std::uint32_t kSamples = 28; // 3 keys * 2 channels * 24

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    void real(std::vector<std::byte>& out, float v)
    {
        append_u32(out, std::bit_cast<std::uint32_t>(v));
    }

    std::vector<std::byte> names()
    {
        std::string text("A:san\0", 6);
        text.resize(kBase, '\0');
        return bytes_of_string(text);
    }

    std::vector<std::byte> symbol_row()
    {
        std::vector<std::byte> out;
        words(out, { 0, kBase });
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> structure(std::uint32_t keys = 3, std::uint32_t channels = 2)
    {
        std::vector<std::byte> out;
        words(out, { keys, channels, kBase + kTimes, kBase + kSamples });
        for (float t : { 0.5f, 1.0f, 1.5f })
            real(out, t);
        for (std::uint32_t i = 0; i < 6; ++i)
        {
            for (float v : { float(i), float(i) + 0.5f, -float(i) })
                real(out, v);
            for (std::uint16_t p : { std::uint16_t(i), std::uint16_t(0x7FFF), std::uint16_t(0x8001), std::uint16_t(0), std::uint16_t(0), std::uint16_t(0) })
            {
                append_u16(out, p);
            }
        }
        REQUIRE(out.size() == kSamples + 6 * 24);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("simple animations expose key times and samples")
{
    const auto overlay = make(structure());
    const auto items = read_simple_anims(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "A");
    REQUIRE(items[0].anim.has_value());

    const auto& a = *items[0].anim;
    CHECK(a.channels == 2);
    REQUIRE(a.times.size() == 3);
    CHECK(a.times[2] == 1.5f);
    REQUIRE(a.samples.size() == 6);
    CHECK(a.samples[3].vector[0] == 3.0f);
    CHECK(a.samples[3].vector[1] == 3.5f);
    CHECK(a.samples[3].packed[0] == 3);
    CHECK(a.samples[3].packed[1] == 0x7FFF);
    CHECK(a.samples[3].packed[2] == -0x7FFF);
}

TEST_CASE("simple animation edge cases")
{
    SUBCASE("a key count that cannot fit is refused")
    {
        const auto overlay = make(structure(0x7FFFFFFF));
        CHECK_FALSE(read_simple_anims(overlay, overlay.common())[0].anim.has_value());
    }

    SUBCASE("samples that leave the block make the animation unreadable")
    {
        const auto overlay = make(structure(3, 3));
        CHECK_FALSE(read_simple_anims(overlay, overlay.common())[0].anim.has_value());
    }

    SUBCASE("an animation without keys reads")
    {
        const auto overlay = make(structure(0));
        const auto items = read_simple_anims(overlay, overlay.common());
        REQUIRE(items[0].anim.has_value());
        CHECK(items[0].anim->times.empty());
        CHECK(items[0].anim->samples.empty());
    }
}
