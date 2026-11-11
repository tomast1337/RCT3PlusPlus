// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/translation_audit.hpp"

#include "ui/translation_file.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

using namespace rct3::ui;

namespace
{
    // A small source tree under the test's temporary folder.
    std::filesystem::path write_tree(std::string_view source)
    {
        const auto root = std::filesystem::temp_directory_path() / "rct3pp_translation_audit";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "src");
        std::ofstream(root / "src" / "a.cpp") << source;
        return root;
    }
} // namespace

TEST_CASE("the scan finds literals, prefixes and lookups, skipping comments")
{
    const auto root = write_tree(R"src(// "Rct3pp_InComment"
/* "Rct3pp_InBlock"
*/ constexpr auto k = "Rct3pp_Named"; char q = '"'; int n = 1'000;
auto s = std::format("Track_{}", name); ///< Followed by a piece's name
auto t = text(strings, "Game_Key");
auto r = R"x(Rct3pp_Raw)x";
)src");
    const CodeKeys code = scan_code(root);
    std::filesystem::remove_all(root);
    CHECK(code.literals.contains("Rct3pp_Named"));
    CHECK(code.literals.at("Game_Key").line == 5);
    CHECK_FALSE(code.literals.contains("Rct3pp_InComment"));
    CHECK_FALSE(code.literals.contains("Rct3pp_InBlock"));
    CHECK_FALSE(code.literals.contains("Rct3pp_Raw"));
    REQUIRE(prefix_of(code, "Track_Straight"));
    CHECK(prefix_of(code, "Track_Straight")->note == "Followed by a piece's name");
    REQUIRE(code.lookups.size() == 1);
    CHECK(code.lookups[0].first == "Game_Key");

    StringTable strings;
    strings.add("Rct3pp_Named", u"x");
    const auto missing = missing_keys(code, strings);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].first == "Game_Key");
}

// The guard on the files built in: run from the repository root, as ./nob test does.
TEST_CASE("every key our code names is in the English files, and each of our keys is named")
{
    const auto english = load_translation(kOwnBaseLanguage);
    REQUIRE_MESSAGE(english, english.error());
    const CodeKeys code = scan_code(".");
    REQUIRE_MESSAGE(!code.literals.empty(), "no sources: the tests run from the repository root");
    for (const auto& [key, place] : missing_keys(code, *english))
        FAIL_CHECK(key << " (" << place.file << ":" << place.line << ") is in no file under assets/lang");
    for (const std::string& key : english->keys())
    {
        if (key.starts_with(kOwnKeyPrefix) && !code.literals.contains(key))
            FAIL_CHECK(key << " is used nowhere: remove it from assets/lang");
    }
}
