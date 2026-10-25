// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal_model.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_animal_models;

namespace
{

    // Name block: "A:mdl" at 0, padded to 16. The item block follows at address 16.
    constexpr std::uint32_t kBase = 16;

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    void reals(std::vector<std::byte>& out, std::initializer_list<float> values)
    {
        for (auto v : values)
            append_u32(out, std::bit_cast<std::uint32_t>(v));
    }

    void align(std::vector<std::byte>& out)
    {
        out.resize((out.size() + 15) & ~std::size_t { 15 });
    }

    void text(std::vector<std::byte>& out, const std::string& s)
    {
        append_u16(out, static_cast<std::uint16_t>(s.size()));
        for (char c : s)
            out.push_back(static_cast<std::byte>(c));
    }

    std::vector<std::byte> header(std::uint8_t levels = 2, std::uint32_t flags = 0x10000)
    {
        std::vector<std::byte> out;
        append_u32(out, flags);
        out.push_back(std::byte { levels });
        out.push_back(std::byte { levels });
        append_u16(out, 1); // one material
        words(out, { 0 });
        reals(out, { 1, 0, -1, 0, -2, 1, 3, 2 }); // two words, then the bounds from 0x14
        out.resize(0x50);
        return out;
    }

    struct Options
    {
        std::int16_t second_parent = 0;
        std::uint8_t vertex_bone = 1;
        std::uint16_t index = 2;
        std::uint32_t format = 0x1305;
    };

    // Two bones, one material "Skin", a level with one triangle and an empty second level.
    std::vector<std::byte> data_chunk(const Options& o = { })
    {
        std::vector<std::byte> out;
        words(out, { 2, 0, 0, 0 });
        align(out);
        for (int bone = 0; bone < 2; ++bone)
        {
            reals(out, { 0, static_cast<float>(bone), 0, 1, 0, 0, 0, 1 });
            for (int k = 0; k < 16; ++k)
                reals(out, { k % 5 == 0 ? 1.0f : 0.0f });
        }
        append_u16(out, 0xFFFF), append_u16(out, 1);
        append_u16(out, static_cast<std::uint16_t>(o.second_parent)), append_u16(out, 0xFFFF);
        align(out);
        words(out, { 5 });
        for (char c : std::string("Skin", 5))
            out.push_back(static_cast<std::byte>(c));
        align(out);
        // Levels: one group and one mesh, then nothing.
        append_u16(out, 1), append_u16(out, 1), words(out, { 2, 0, 0, 0 });
        append_u16(out, 1), append_u16(out, 0), words(out, { 2, 0, 0, 0 });
        words(out, { 0, 0 });
        align(out);
        words(out, { o.format, 3 });
        append_u16(out, 1), append_u16(out, 3);
        out.resize(out.size() + 20);
        align(out);
        for (int v = 0; v < 3; ++v)
        {
            reals(out, { static_cast<float>(v), 1, 2, 0, 1, 0 });
            out.push_back(std::byte { 0 }), out.push_back(std::byte { o.vertex_bone }), out.push_back(std::byte { 0 }), out.push_back(std::byte { 0 });
            out.push_back(std::byte { 55 }), out.push_back(std::byte { 200 }), out.push_back(std::byte { 0 }), out.push_back(std::byte { 0 });
            words(out, { 0xFFFFFFFF });
            reals(out, { 0.25f, 0.75f });
        }
        append_u16(out, 0), append_u16(out, 1), append_u16(out, o.index);
        out.resize((out.size() + 3) & ~std::size_t { 3 });
        append_u16(out, 0), append_u16(out, 1), append_u16(out, 0), append_u16(out, 0), words(out, { 0, 0 });
        words(out, { 0x10000 });
        // The empty level's group.
        append_u16(out, 0), append_u16(out, 0), append_u16(out, 0), append_u16(out, 0), words(out, { 0, 0 });
        return out;
    }

    std::vector<std::byte> chunks(const std::vector<std::byte>& data, const std::string& names = std::string("root\0leg\0", 9))
    {
        std::vector<std::byte> out;
        append_u32(out, static_cast<std::uint32_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
        append_u32(out, static_cast<std::uint32_t>(names.size()));
        for (char c : names)
            out.push_back(static_cast<std::byte>(c));
        return out;
    }

    Overlay load(const std::vector<std::byte>& item_bytes, const std::vector<std::byte>& tail)
    {
        std::string n("A:mdl\0", 6);
        n.resize(kBase, '\0');
        std::vector<std::byte> symbol;
        words(symbol, { 0, kBase, 1 });
        std::vector<std::byte> loader;
        words(loader, { 0, kBase, 2, 0, 0 });
        auto file = build_file(1, { { bytes_of_string(n) }, { item_bytes }, { symbol, loader } }, { }, tail);
        constexpr std::size_t kLoaderCount = 20;
        file[kLoaderCount] = std::byte { 1 };
        std::vector<std::byte> declaration;
        text(declaration, "FGDK");
        text(declaration, "Model");
        append_u32(declaration, 16);
        text(declaration, "mdl");
        file.insert(file.begin() + kLoaderCount + 4, declaration.begin(), declaration.end());
        auto overlay = Overlay::from_bytes(file, std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("animal models expose bones, levels and meshes with their materials")
{
    const auto overlay = load(header(), chunks(data_chunk()));
    const auto items = read_animal_models(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "A");
    REQUIRE(items[0].model.has_value());
    const auto& m = *items[0].model;
    CHECK(m.bounds_min[2] == -2.0f);
    CHECK(m.bounds_max[1] == 3.0f);
    REQUIRE(m.bones.size() == 2);
    CHECK(m.bones[0].name == "root");
    CHECK(m.bones[0].parent == -1);
    CHECK(m.bones[1].name == "leg");
    CHECK(m.bones[1].parent == 0);
    CHECK(m.bones[1].position[1] == 1.0f);
    CHECK(m.bones[1].rotation[3] == 1.0f);
    REQUIRE(m.levels.size() == 2);
    CHECK(m.levels[1].empty());
    REQUIRE(m.levels[0].size() == 1);
    const auto& mesh = m.levels[0][0];
    CHECK(mesh.texture == "Skin");
    CHECK(mesh.vertex_count == 3);
    CHECK(mesh.index_count == 3);
    CHECK(mesh.index(2) == 2);
    const auto v = mesh.vertex(2);
    CHECK(v.position[0] == 2.0f);
    CHECK(v.bones[1] == 1);
    CHECK(v.weights[1] == 200);
    CHECK(v.texcoord[1] == 0.75f);
}

TEST_CASE("animal model edge cases")
{
    const auto unreadable = [](const Overlay& overlay) {
        const auto items = read_animal_models(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        return !items[0].model.has_value();
    };
    SUBCASE("a parent after its child is refused")
    {
        CHECK(unreadable(load(header(), chunks(data_chunk({ .second_parent = 1 })))));
    }
    SUBCASE("a vertex weighted to a bone past the skeleton is refused")
    {
        CHECK(unreadable(load(header(), chunks(data_chunk({ .vertex_bone = 2 })))));
    }
    SUBCASE("an index past the vertices is refused")
    {
        CHECK(unreadable(load(header(), chunks(data_chunk({ .index = 3 })))));
    }
    SUBCASE("another vertex layout is not supported")
    {
        CHECK(unreadable(load(header(), chunks(data_chunk({ .format = 0x1105 })))));
    }
    SUBCASE("levels kept outside the chunk are not supported")
    {
        CHECK(unreadable(load(header(2, 0x90000), chunks(data_chunk()))));
    }
    SUBCASE("too few bone names make the item unreadable")
    {
        CHECK(unreadable(load(header(), chunks(data_chunk(), std::string("root\0", 5)))));
    }
    SUBCASE("a chunk cut short makes the item unreadable")
    {
        auto data = data_chunk();
        data.resize(data.size() - 8);
        CHECK(unreadable(load(header(), chunks(data))));
    }
}
