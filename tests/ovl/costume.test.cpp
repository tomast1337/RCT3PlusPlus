// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/costume.hpp"

#include "builder.hpp"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_costumes;

namespace
{

    constexpr char kNameData[] = "Bear:ent\0BearHead\0BearBody\0BearLegs\0Grizzly";
    const std::string kNames(kNameData, sizeof kNameData);
    const std::uint32_t kBase = (static_cast<std::uint32_t>(sizeof kNameData) + 3) & ~3u;

    std::uint32_t name_at(const std::string& needle)
    {
        const auto at = kNames.find(needle);
        REQUIRE(at != std::string::npos);
        return static_cast<std::uint32_t>(at);
    }

    Overlay make(std::uint32_t count)
    {
        std::string text = kNames;
        text.resize(kBase, '\0');
        // The costume, then its five section slots: head, body, legs and two left empty.
        std::vector<std::byte> data;
        for (const std::uint32_t v : { 0u, 0u, 0u, count, kBase + 0x18, name_at("Grizzly"), name_at("BearHead"), name_at("BearBody"), name_at("BearLegs"), 0u, 0u })
            append_u32(data, v);
        std::vector<std::byte> symbol;
        append_u32(symbol, name_at("Bear:ent"));
        append_u32(symbol, kBase);
        append_u16(symbol, 1);
        append_u16(symbol, 0xFFFF);
        append_u32(symbol, 0);
        auto overlay = Overlay::from_bytes(build_file(5, { { bytes_of_string(text) }, { data }, { symbol, { }, { } } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("a costume names its sections' part sets, head first")
{
    const auto overlay = make(5);
    const auto costumes = read_costumes(overlay, overlay.common());
    REQUIRE(costumes.size() == 1);
    CHECK(costumes[0].name == "Bear");
    REQUIRE(costumes[0].costume.has_value());
    CHECK(costumes[0].costume->name == "Grizzly");
    REQUIRE(costumes[0].costume->sections.size() == 5);
    CHECK(costumes[0].costume->sections[0] == "BearHead");
    CHECK(costumes[0].costume->sections[2] == "BearLegs");
    CHECK(costumes[0].costume->sections[4].empty());
}

TEST_CASE("a costume with a huge section count is unreadable")
{
    const auto overlay = make(1u << 20);
    const auto costumes = read_costumes(overlay, overlay.common());
    REQUIRE(costumes.size() == 1);
    CHECK_FALSE(costumes[0].costume.has_value());
}
