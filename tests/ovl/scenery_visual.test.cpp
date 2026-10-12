// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/scenery_visual.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_scenery_visuals;

namespace
{

    const std::string kNames("V:svd\0Lod0\0Lod1\0Sh:shs\0Bo:bsh\0An:ban\0Bb:ftx\0", 44);
    const std::uint32_t kBase = 48; // names padded to a multiple of 4

    std::uint32_t name_at(const std::string& needle)
    {
        const auto at = kNames.find(needle);
        REQUIRE(at != std::string::npos);
        return static_cast<std::uint32_t>(at);
    }

    // Offsets inside the structure block.
    constexpr std::uint32_t kHeader = 0;      // 28 bytes
    constexpr std::uint32_t kLodArray = 28;   // 2 pointers
    constexpr std::uint32_t kLod0 = 36;       // 72 bytes
    constexpr std::uint32_t kLod1 = 108;      // 72 bytes
    constexpr std::uint32_t kAnimArray = 180; // 2 pointers
    constexpr std::uint32_t kAnimSlots = 188; // 2 reference fields
    constexpr std::uint32_t kEnd = 196;

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
        std::string text = kNames;
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

    std::vector<std::byte> reference_row(std::uint32_t location, const std::string& name)
    {
        std::vector<std::byte> out;
        words(out, { location, name_at(name), 0, 0 });
        return out;
    }

    std::vector<std::byte> concat(std::vector<std::vector<std::byte>> parts)
    {
        std::vector<std::byte> out;
        for (auto& p : parts)
            out.insert(out.end(), p.begin(), p.end());
        return out;
    }

    std::vector<std::byte> all_references()
    {
        return concat({ reference_row(kBase + kLod0 + 8, "Sh:shs"), reference_row(kBase + kLod1 + 16, "Bo:bsh"), reference_row(kBase + kAnimSlots, "An:ban"), reference_row(kBase + kAnimSlots + 4, "An:ban") });
    }

    void lod(std::vector<std::byte>& out, std::uint32_t type, std::uint32_t name, float distance, std::uint32_t animations, std::uint32_t animation_array)
    {
        words(out, { type, name, 0, 0, 0, 0, 0, 0 });
        for (float f : { 2.0f, 3.0f, 0.1f, 0.9f, 0.2f, 0.8f, distance })
            real(out, f);
        words(out, { animations, 0, animation_array });
    }

    std::vector<std::byte> structure(std::uint32_t lod_count = 2, std::uint32_t animation_array = kBase + kAnimArray)
    {
        std::vector<std::byte> out;
        words(out, { 5 });
        for (float f : { 0.5f, 0.75f, 1.0f, 0.25f })
            real(out, f);
        words(out, { lod_count, kBase + kLodArray, kBase + kLod0, kBase + kLod1 });
        lod(out, 0, name_at("Lod0"), 50.0f, 0, 0);
        lod(out, 3, name_at("Lod1"), 300.0f, 2, animation_array);
        words(out, { kBase + kAnimSlots, kBase + kAnimSlots + 4, 0, 0 });
        REQUIRE(out.size() == kEnd);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block, const std::vector<std::byte>& refs = all_references())
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row(), { }, refs } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("scenery visuals expose levels of detail and references")
{
    const auto overlay = make(structure());
    const auto items = read_scenery_visuals(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "V");
    REQUIRE(items[0].visual.has_value());

    const auto& v = *items[0].visual;
    CHECK(v.flags == 5);
    CHECK(v.sway == 0.5f);
    CHECK(v.brightness == 0.75f);
    CHECK(v.scale_variation == 0.25f);
    REQUIRE(v.lods.size() == 2);

    const auto& a = v.lods[0];
    CHECK(a.mesh_type == static_cast<std::uint32_t>(rct3::ovl::LodMeshType::StaticShape));
    CHECK(a.name == "Lod0");
    CHECK(a.static_shape == "Sh");
    CHECK(a.bone_shape.empty());
    CHECK(a.width == 2.0f);
    CHECK(a.height == 3.0f);
    CHECK(a.texcoords[1] == 0.9f);
    CHECK(a.texcoords[2] == 0.2f);
    CHECK(a.distance == 50.0f);
    CHECK(a.animations.empty());

    const auto& b = v.lods[1];
    CHECK(b.mesh_type == static_cast<std::uint32_t>(rct3::ovl::LodMeshType::BoneShape));
    CHECK(b.name == "Lod1");
    CHECK(b.bone_shape == "Bo");
    CHECK(b.static_shape.empty());
    CHECK(b.distance == 300.0f);
    REQUIRE(b.animations.size() == 2);
    CHECK(b.animations[0] == "An");
    CHECK(b.animations[1] == "An");
}

TEST_CASE("scenery visual edge cases")
{
    SUBCASE("a lod count that cannot fit is refused")
    {
        const auto overlay = make(structure(0x7FFFFFFF));
        const auto items = read_scenery_visuals(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].visual.has_value());
    }

    SUBCASE("an animation array that leaves its block makes the visual unreadable")
    {
        const auto overlay = make(structure(2, kBase + kEnd - 4));
        const auto items = read_scenery_visuals(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].visual.has_value());
    }

    SUBCASE("a missing reference table leaves names empty")
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { structure() }, { symbol_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        const auto items = read_scenery_visuals(*overlay, overlay->common());
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].visual.has_value());
        CHECK(items[0].visual->lods[0].static_shape.empty());
        CHECK(items[0].visual->lods[1].animations.size() == 2);
        CHECK(items[0].visual->lods[1].animations[0].empty());
        CHECK(items[0].visual->lods[0].name == "Lod0");
    }

    SUBCASE("a reference of the wrong type is not used")
    {
        const auto refs = concat({ reference_row(kBase + kLod0 + 8, "Bo:bsh") });
        const auto overlay = make(structure(), refs);
        const auto items = read_scenery_visuals(overlay, overlay.common());
        REQUIRE(items[0].visual.has_value());
        CHECK(items[0].visual->lods[0].static_shape.empty());
    }
}
