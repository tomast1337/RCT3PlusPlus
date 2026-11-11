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

// The built-in files load as one: names from every file are found, with their text, as the game's windows ask.
TEST_CASE("the English files load whole: ride, scenario and pack names among them")
{
    const auto english = load_translation(kOwnBaseLanguage);
    REQUIRE_MESSAGE(english, english.error());
    CHECK(english->get("AirPoweredRCName") == u"Air Powered Coaster");          // a ride kind's name
    CHECK(english->get("AccessoriesStallName") == u"Accessories");              // a stall's name
    CHECK(english->get("Scenario01_Lookup") == u"VanillaHills");                // the first career scenario
    CHECK(english->get("AOP0_Scenario01_Lookup") == u"CaptainBlackheartsCove"); // the first expansion's
    CHECK(english->get("AOP_FSName_1") == u"Wild");                             // an expansion's folder
    CHECK(english->get("SceneryItem_1m") == u"1m support");
    CHECK(english->get("TrackDesign_4DTerror") == u"4D Terror");
    CHECK(english->get("CarriedItemExtra_Cheese") == u"Cheese");
    CHECK(english->get("AttractionName_LoopingWasp") == u"Looping Wasp");
    CHECK(english->keys().size() > 13'000);
}
