// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/spline.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_splines;

namespace
{

    // Name block: "A:spl" at 0, padded to 16. The structure block follows at address 16.
    constexpr std::uint32_t kBase = 16;
    constexpr std::uint32_t kHeader = 0; // 32 bytes
    constexpr std::uint32_t kNodes = 32; // 3 * 36
    constexpr std::uint32_t kLengths = 140;

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    void real(std::vector<std::byte>& out, float v)
    {
        append_u32(out, std::bit_cast<std::uint32_t>(v));
    }

    void node(std::vector<std::byte>& out, float x, float y, float z, float in, float out_t)
    {
        for (float f : { x, y, z, in, 0.0f, 0.0f, out_t, 0.0f, 0.0f })
            real(out, f);
    }

    std::vector<std::byte> names()
    {
        std::string text("A:spl\0", 6);
        text.resize(kBase, '\0');
        return bytes_of_string(text);
    }

    std::vector<std::byte> symbol_row()
    {
        std::vector<std::byte> out;
        words(out, { 0, kBase + kHeader });
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> structure(std::uint32_t count = 3, std::uint32_t closed = 0)
    {
        std::vector<std::byte> out;
        words(out, { count, kBase + kNodes, closed });
        real(out, 5.0f);
        real(out, 0.2f);
        words(out, { kBase + kLengths, 0 });
        real(out, 3.0f);
        node(out, 0, 1, 0, 0, 1);
        node(out, 2, 2, 0, -1, 1);
        node(out, 5, 3, 0, -1, 0);
        REQUIRE(out.size() == kLengths);
        real(out, 2.0f);
        real(out, 3.0f);
        if (closed)
            real(out, 4.0f);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("splines expose nodes and segment lengths")
{
    const auto overlay = make(structure());
    const auto items = read_splines(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "A");
    REQUIRE(items[0].spline.has_value());

    const auto& s = *items[0].spline;
    CHECK_FALSE(s.closed);
    CHECK(s.length == 5.0f);
    CHECK(s.max_height == 3.0f);
    REQUIRE(s.nodes.size() == 3);
    CHECK(s.nodes[1].position[0] == 2.0f);
    CHECK(s.nodes[1].tangent_in[0] == -1.0f);
    CHECK(s.nodes[1].tangent_out[0] == 1.0f);
    REQUIRE(s.segment_lengths.size() == 2);
    CHECK(s.segment_lengths[0] == 2.0f);
    CHECK(s.segment_lengths[1] == 3.0f);
}

TEST_CASE("spline edge cases")
{
    SUBCASE("a closed spline has one more segment")
    {
        const auto overlay = make(structure(3, 1));
        const auto items = read_splines(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].spline.has_value());
        CHECK(items[0].spline->closed);
        REQUIRE(items[0].spline->segment_lengths.size() == 3);
        CHECK(items[0].spline->segment_lengths[2] == 4.0f);
    }

    SUBCASE("a node count that cannot fit is refused")
    {
        const auto overlay = make(structure(0x7FFFFFFF));
        CHECK_FALSE(read_splines(overlay, overlay.common())[0].spline.has_value());
    }

    SUBCASE("a node run that leaves its block makes the spline unreadable")
    {
        const auto overlay = make(structure(4));
        CHECK_FALSE(read_splines(overlay, overlay.common())[0].spline.has_value());
    }

    SUBCASE("a spline without nodes reads")
    {
        const auto overlay = make(structure(0));
        const auto items = read_splines(overlay, overlay.common());
        REQUIRE(items[0].spline.has_value());
        CHECK(items[0].spline->nodes.empty());
        CHECK(items[0].spline->segment_lengths.empty());
    }
}
