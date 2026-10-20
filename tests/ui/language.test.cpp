// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/language.hpp"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::ui;

TEST_CASE("languages are found by their overlay names, in the known order")
{
    const std::vector<std::string> files = { "german.common.ovl", "German.unique.ovl", "English.common.ovl", "Klingon.common.ovl", "SChinese.common.ovl", "English.common.ovl.bak", "Resolution.common.ovl" };
    CHECK(languages_in(files) == std::vector<std::string> { "English", "German", "SChinese" });
    CHECK(languages_in({ }).empty());
}

TEST_CASE("a language is looked up ignoring case")
{
    REQUIRE(find_language("schinese"));
    CHECK(find_language("schinese")->name == "SChinese");
    CHECK(find_language("SChinese")->cjk);
    CHECK_FALSE(find_language("German")->cjk);
    CHECK(!find_language("Esperanto"));
}

TEST_CASE("a locale names its language")
{
    CHECK(language_for_locale("de_DE.UTF-8") == "German");
    CHECK(language_for_locale("fr") == "French");
    CHECK(language_for_locale("pt_BR.UTF-8@x") == "BrazilianPortuguese");
    CHECK(language_for_locale("zh_TW.UTF-8") == "Chinese");
    CHECK(language_for_locale("zh_CN.UTF-8") == "SChinese");
    CHECK(language_for_locale("nn_NO") == "Norwegian");
    CHECK(language_for_locale("ja_JP") == "Japanese");
    CHECK(language_for_locale("C").empty());
    CHECK(language_for_locale("C.UTF-8").empty());
    CHECK(language_for_locale("").empty());
    CHECK(language_for_locale("dexx").empty()); // A prefix must end at the territory
}

TEST_CASE("the language shown is the one set, else the locale's, else English, else the first")
{
    const std::vector<std::string> installed = { "English", "French", "German" };
    CHECK(choose_language(installed, "german", "fr_FR") == "German");
    CHECK(choose_language(installed, "Danish", "fr_FR") == "French");
    CHECK(choose_language(installed, "", "da_DK") == "English");
    CHECK(choose_language(std::vector<std::string> { "Dutch", "Italian" }, "", "C") == "Dutch");
    CHECK(choose_language({ }, "German", "de").empty());
}

TEST_CASE("each language's strings are in its own overlay")
{
    CHECK(text_overlay("German") == "gui/German.common.ovl");
}
