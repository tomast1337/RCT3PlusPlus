// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/about_window.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    StringTable sample_strings()
    {
        StringTable s;
        s.add("ProgramInfoDialog_Title", u"About the game");
        s.add("ProgramInfoDialog_ProgramInfoTabHelp", u"Info");
        s.add("ProgramInfoDialog_CreditsPanelTabHelp", u"Credits");
        s.add("ProgramInfo_Title", u"A Park Game");
        s.add("ProgramInfo_Copyright1", u"Made by someone.");
        s.add("ProgramInfo_Copyright2", u"All rights kept.");
        s.add("ProgramInfo_Copyright4", u"Never read: 3 is missing");
        s.add("RCT3_Name", u"The Game");
        s.add("ProgramInfoDialog_GeneralCredits0", u"Makers|Ann|Bob");
        s.add("ProgramInfoDialog_GeneralCredits1", u"Tester|Cy");
        s.add("ContentPackName_WildAddOn", u"Second pack");
        s.add("ProgramInfoDialog_GeneralCredits_AOP1_0", u"Pack maker|Di");
        s.add("ContentPackName_SoakedAddOn", u"First pack");
        return s;
    }
} // namespace

TEST_CASE("the info page has the game's name and the copyright lines up to the first missing")
{
    const StringTable s = sample_strings();
    CHECK(about_info_lines(s) == std::vector<std::u16string> { u"A Park Game", u"Made by someone.", u"All rights kept." });
}

TEST_CASE("the game's credits and copyright lines are copied in, and nothing else")
{
    StringTable ours;
    ours.add("ProgramInfo_Title", u"Ours");
    add_game_credits(ours, sample_strings());
    CHECK(ours.get("ProgramInfo_Title") == u"Ours");
    CHECK(ours.get("ProgramInfo_Copyright2") == u"All rights kept.");
    CHECK_FALSE(ours.contains("ProgramInfo_Copyright4"));
    CHECK(ours.get("ProgramInfoDialog_GeneralCredits1") == u"Tester|Cy");
    CHECK(ours.get("ProgramInfoDialog_GeneralCredits_AOP1_0") == u"Pack maker|Di");
    CHECK_FALSE(ours.contains("RCT3_Name"));
}

TEST_CASE("credits split into a role and its people, packs first, alternating sides")
{
    const StringTable s = sample_strings();
    random::RandomState r = random::seeded(random::Seed { 7 });
    const std::vector<int> packs { 0, 1 }; // Pack 0 has no credits here, so it has no heading either
    const auto lines = credit_lines(s, packs, r);
    REQUIRE(lines.size() == 9);
    CHECK(lines[0].text == u"Second pack");
    CHECK(lines[0].kind == CreditLine::Kind::Heading);
    CHECK(lines[1].text == u"Pack maker");
    CHECK(lines[1].kind == CreditLine::Kind::Role);
    CHECK(lines[2].text == u"Di");
    CHECK(lines[2].kind == CreditLine::Kind::Name);
    CHECK(lines[3].text == u"The Game");
    CHECK(lines[4].text == u"Makers");
    CHECK(lines[4].align == Align::End);
    CHECK(lines[5].text == u"Ann");
    CHECK(lines[6].text == u"Bob");
    CHECK(lines[7].text == u"Tester");
    CHECK(lines[7].align == Align::Start);
    for (const CreditLine& l : lines)
    {
        CHECK(l.inset >= 0);
        CHECK(l.inset < 1);
    }
    // A credit's lines keep its side and inset, one row after another.
    CHECK(lines[5].inset == lines[4].inset);
    CHECK(lines[6].row == lines[5].row + 1);
    CHECK(lines[7].gaps == lines[6].gaps + 1);
}

TEST_CASE("the tabs pick the page and its caption; the credits roll and start over")
{
    const StringTable s = sample_strings();
    Desktop desktop;
    auto& about = desktop.root().add<GameInfoBox>("about", game_skins::about_window_style(kGamePalette), s, AddOnPacks { }, random::Seed { 1 });
    desktop.layout({ 0, 0, 1024, 768 });
    desktop.update({ });
    CHECK(about.page() == AboutPage::Info);
    CHECK(about.caption().text == u"Info");
    CHECK(about.info_lines().size() == 3);

    about.tab(AboutPage::Credits).click();
    desktop.update({ });
    CHECK(about.page() == AboutPage::Credits);
    CHECK(about.caption().text == u"Credits");
    CHECK(about.tab(AboutPage::Credits).on);
    CHECK_FALSE(about.tab(AboutPage::Info).on);
    // Without fonts nothing has a height, so nothing rolls.
    CHECK(about.credits().offset() == 0);
}
