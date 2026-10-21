// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/overlay.hpp"

#include "builder.hpp"

#include <atomic>
#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Error;
using rct3::ovl::Overlay;

namespace
{

    namespace fs = std::filesystem;

    std::vector<std::byte> common_bytes()
    {
        // One string at address 0: "gamma:txt".
        return build_file(5, { { bytes_of_string(std::string("gamma:txt\0", 10)) } }, { });
    }

    std::vector<std::byte> unique_bytes()
    {
        // One symbol row whose name points at the common file's string (address 0).
        std::vector<std::byte> row;
        append_u32(row, 0);
        append_u32(row, 0x1234);
        append_u16(row, 1);
        append_u16(row, 0xFFFF);
        append_u32(row, 7);
        return build_file(5, { { }, { }, { row } }, { });
    }

    void write(const fs::path& path, const std::vector<std::byte>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // A scratch directory removed on scope exit.
    struct TempDir
    {
        fs::path path;
        TempDir()
        {
            static std::atomic<int> counter { 0 };
            path = fs::temp_directory_path() / std::format("rct3pp_overlay_test_{}_{}", counter++, reinterpret_cast<std::uintptr_t>(this));
            fs::create_directories(path);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

} // namespace

TEST_CASE("from_bytes resolves the unique file's names through the common file")
{
    auto overlay = Overlay::from_bytes(common_bytes(), unique_bytes());
    REQUIRE(overlay.has_value());

    CHECK(overlay->common().base == 0);
    REQUIRE(overlay->unique());
    CHECK(overlay->unique()->base == overlay->common().file.data_size);

    REQUIRE(overlay->unique()->symbols.size() == 1);
    CHECK(overlay->unique()->symbols[0].name == "gamma:txt");
    CHECK(overlay->unique()->symbols[0].data == 0x1234);

    CHECK(overlay->space().cstring_at(0) == "gamma:txt");
}

TEST_CASE("an overlay survives being moved")
{
    auto overlay = Overlay::from_bytes(common_bytes(), unique_bytes());
    REQUIRE(overlay.has_value());

    Overlay moved = std::move(*overlay);
    REQUIRE(moved.unique());
    REQUIRE(moved.unique()->symbols.size() == 1);
    // The name views must still point at live bytes after the move.
    CHECK(moved.unique()->symbols[0].name == "gamma:txt");
}

TEST_CASE("a common file alone loads without a unique part")
{
    auto overlay = Overlay::from_bytes(common_bytes(), std::nullopt);
    REQUIRE(overlay.has_value());
    CHECK(!overlay->unique());
}

TEST_CASE("bad bytes surface the parser error")
{
    CHECK(Overlay::from_bytes(bytes_of({ 1, 2, 3 }), std::nullopt).error() == Error::Truncated);
    CHECK(Overlay::from_bytes(common_bytes(), bytes_of({ 'N', 'O', 'P', 'E', 0, 0, 0, 0, 5, 0, 0, 0 })).error() == Error::BadMagic);
}

TEST_CASE("load finds the pair from either file or the stem")
{
    TempDir dir;
    write(dir.path / "demo.common.ovl", common_bytes());
    write(dir.path / "demo.unique.ovl", unique_bytes());

    for (const auto& name : { "demo.common.ovl", "demo.unique.ovl", "demo" })
    {
        CAPTURE(name);
        auto overlay = Overlay::load(dir.path / name);
        REQUIRE(overlay.has_value());
        REQUIRE(overlay->unique());
        CHECK(overlay->unique()->symbols[0].name == "gamma:txt");
    }
}

TEST_CASE("load tolerates a missing unique file but not a missing common file")
{
    TempDir dir;
    write(dir.path / "solo.common.ovl", common_bytes());
    write(dir.path / "orphan.unique.ovl", unique_bytes());

    auto solo = Overlay::load(dir.path / "solo.common.ovl");
    REQUIRE(solo.has_value());
    CHECK(!solo->unique());

    CHECK(Overlay::load(dir.path / "orphan.unique.ovl").error() == Error::MissingFile);
    CHECK(Overlay::load(dir.path / "nothing").error() == Error::MissingFile);
}
