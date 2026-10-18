// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/translation_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("a translation is an object of strings, groups joining their names")
{
    const auto table = parse_translation(R"({ "Rct3pp": { "A": "First", "Group": { "B": "Second" } }, "Top": "" })");
    REQUIRE(table);
    CHECK(table->size() == 3);
    CHECK(table->get("Rct3pp_A") == u"First");
    CHECK(table->get("Rct3pp_Group_B") == u"Second");
    CHECK(table->contains("Top"));
}

TEST_CASE("strings are UTF-8, escapes as JSON has them")
{
    const auto table = parse_translation(
        "{ \"Rct3pp_A\": \"Gr\xC3\xBC\xC3\x9F"
        "e\\nzwei \\u00e9\" }");
    REQUIRE(table);
    CHECK(table->get("Rct3pp_A") == u"Gr\u00FC\u00DFe\nzwei \u00E9");
}

TEST_CASE("anything but strings and groups fails, saying where")
{
    const auto number = parse_translation(R"({ "Rct3pp": { "A": 3 } })");
    REQUIRE_FALSE(number);
    CHECK(number.error().starts_with("Rct3pp_A"));
    CHECK_FALSE(parse_translation("not json"));
    CHECK_FALSE(parse_translation("[\"a\"]"));
    CHECK_FALSE(parse_translation(R"({ "": "x" })"));
    std::string deep;
    for (int i = 0; i <= kMaxTranslationDepth + 1; ++i)
        deep += "{\"g\":";
    deep += "\"x\"" + std::string(static_cast<std::size_t>(kMaxTranslationDepth + 2), '}');
    CHECK_FALSE(parse_translation(deep));
}

TEST_CASE("translation files are named by language and region code")
{
    CHECK(translation_path("English") == "lang/en-US.json");
    CHECK(translation_path("braziliANPortuguese") == "lang/pt-BR.json");
}

TEST_CASE("our built-in English parses and backs other languages")
{
    const StringTable english = own_strings(kOwnBaseLanguage);
    CHECK_FALSE(english.get("Rct3pp_UsualKeys").empty());
    CHECK_FALSE(english.get("Rct3pp_Command_Copy").empty());
    // It also has the keys the game's windows use, with line breaks as real characters.
    CHECK_FALSE(english.get("ProgramInfoDialog_Title").empty());
    CHECK(english.get("TutorialText_Tutorial1_Step2_Text").find(u'\n') != std::u16string::npos);
    // A language without a file of its own falls back to English.
    CHECK(own_strings("Klingon").get("Rct3pp_UsualKeys") == english.get("Rct3pp_UsualKeys"));
}
