// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/skeleton.hpp"

#include "builder.hpp"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_skeletons;

namespace
{

    const std::string kNames("B:sal\0W:san\0Root\0Arm\0Hand\0Wave\0", 31);
    constexpr std::uint32_t kBase = 32; // names padded

    std::uint32_t name_at(const std::string& needle)
    {
        const auto at = kNames.find(needle);
        REQUIRE(at != std::string::npos);
        return static_cast<std::uint32_t>(at);
    }

    // Offsets inside the structure block: the skeleton, then its lists.
    constexpr std::uint32_t kBones = 40;
    constexpr std::uint32_t kParents = 52;
    constexpr std::uint32_t kMotionNames = 64;
    constexpr std::uint32_t kMotions = 68;
    constexpr std::uint32_t kEnd = 72;

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
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
        words(out, { name_at("B:sal"), kBase });
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> reference_row()
    {
        std::vector<std::byte> out;
        words(out, { kBase + kMotions, name_at("W:san"), 0, 0 });
        return out;
    }

    /// Three bones: the second hangs from the first, the third names itself, which counts as none.
    std::vector<std::byte> structure(std::uint32_t bones = 3)
    {
        std::vector<std::byte> out;
        words(out, { bones, kBase + kBones, kBase + kParents, 0, 0, 0, 0, 1, kBase + kMotionNames, kBase + kMotions });
        words(out, { name_at("Root"), name_at("Arm"), name_at("Hand") });
        words(out, { static_cast<std::uint32_t>(-1), 0, 2 });
        words(out, { name_at("Wave"), 0 });
        REQUIRE(out.size() == kEnd);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row(), { }, reference_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("a skeleton lists its bones, what each hangs from and its motions")
{
    const auto overlay = make(structure());
    const auto items = read_skeletons(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "B");
    REQUIRE(items[0].skeleton.has_value());
    const auto& s = *items[0].skeleton;
    CHECK(s.bones == std::vector<std::string_view> { "Root", "Arm", "Hand" });
    CHECK(s.parents == std::vector<std::int32_t> { -1, 0, -1 });
    CHECK(s.animation_names == std::vector<std::string_view> { "Wave" });
    CHECK(s.animations == std::vector<std::string_view> { "W" });
}

TEST_CASE("skeleton edge cases")
{
    SUBCASE("a bone count that cannot fit is refused")
    {
        const auto overlay = make(structure(100000));
        CHECK_FALSE(read_skeletons(overlay, overlay.common())[0].skeleton.has_value());
    }

    SUBCASE("bone lists that leave the block make the skeleton unreadable")
    {
        const auto overlay = make(structure(9));
        CHECK_FALSE(read_skeletons(overlay, overlay.common())[0].skeleton.has_value());
    }
}
