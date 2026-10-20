// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/front_screen.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    StringTable strings()
    {
        StringTable t;
        t.add("FrontScreen_Play_TT", u"Ride");
        t.add("FrontScreen_Play_Help", u"Go riding");
        t.add("FrontScreen_Options_TT", u"Settings");
        t.add("FrontScreen_Exit_TT", u"Leave");
        return t;
    }

    // Updates until everything has arrived.
    void settle(Desktop& desktop)
    {
        for (int i = 0; i < 300; ++i)
            desktop.update({ }, { 1.0f / 30, nullptr });
    }

    struct Fixture
    {
        StringTable table = strings();
        Desktop desktop;
        FrontScreen* screen = nullptr;

        explicit Fixture(std::array<bool, kExpansionCount> expansions = { true, true })
        {
            screen = &desktop.root().add<FrontScreen>("front", table, expansions, "English");
            desktop.layout({ 0, 0, 1024, 768 });
        }
        void click(std::optional<FrontButton&> b)
        {
            REQUIRE(b);
            b->click();
            desktop.update({ }, { 1.0f / 30, nullptr });
        }
    };
} // namespace

TEST_CASE("every menu entry is reachable and every sub-menu returns to its parent")
{
    for (std::size_t m = 0; m < kFrontMenuCount; ++m)
    {
        const FrontMenuInfo& info = front_menu(static_cast<FrontMenu>(m));
        CHECK_FALSE(info.entries.empty());
        CHECK(info.entries.size() <= front_screen::kMaxMenuEntries);
        for (const FrontEntry& e : info.entries)
        {
            CHECK_FALSE(e.label_key.empty());
            CHECK_FALSE(e.icon.empty());
            if (e.action == FrontAction::OpenMenu)
                CHECK(front_menu(e.menu).parent == static_cast<FrontMenu>(m));
        }
    }
}

TEST_CASE("buttons open menus, back returns, other buttons are requests")
{
    Fixture f;
    CHECK(f.screen->current() == FrontMenu::Main);
    CHECK_FALSE(f.screen->back_button().visible);

    f.click(f.screen->button(FrontMenu::Main, 0)); // Play
    CHECK(f.screen->current() == FrontMenu::Play);
    CHECK(f.screen->back_button().visible);
    CHECK_FALSE(f.screen->take_request());

    f.click(f.screen->button(FrontMenu::Play, 2)); // Sandbox
    CHECK(f.screen->current() == FrontMenu::Sandbox);
    f.click(f.screen->button(FrontMenu::Sandbox, 0));
    CHECK(f.screen->take_request() == FrontRequest { FrontAction::NewSandbox, 0 });

    f.click(f.screen->back_button());
    CHECK(f.screen->current() == FrontMenu::Play);
    CHECK(f.screen->on_key({ Key::Escape }));
    CHECK(f.screen->current() == FrontMenu::Main);
    CHECK_FALSE(f.screen->on_key({ Key::Escape })); // Nothing further back

    f.click(f.screen->button(FrontMenu::Main, 2));
    CHECK(f.screen->take_request() == FrontRequest { FrontAction::Options, 0 });
    f.click(f.screen->quit_button());
    CHECK(f.screen->take_request() == FrontRequest { FrontAction::Quit, 0 });
    CHECK_FALSE(f.screen->take_request());
}

TEST_CASE("only the open menu's buttons count, and only those of installed packs work")
{
    Fixture f({ true, false });
    f.click(f.screen->button(FrontMenu::Tools, 0)); // Not open: ignored
    CHECK_FALSE(f.screen->take_request());

    f.screen->open(FrontMenu::Campaigns);
    CHECK(f.screen->button(FrontMenu::Campaigns, 1)->enabled);
    CHECK_FALSE(f.screen->button(FrontMenu::Campaigns, 2)->enabled);
    f.click(f.screen->button(FrontMenu::Campaigns, 1));
    CHECK(f.screen->take_request() == FrontRequest { FrontAction::Campaign, 1 });
    // One badge, for the installed pack.
    CHECK(f.screen->find("badge0"));
    CHECK(!f.screen->find("badge1"));
}

TEST_CASE("the last pack's badge comes first round the circle")
{
    Fixture f;
    f.desktop.update({ }, { 1.0f / 30, nullptr });
    auto first = f.screen->find("badge0");
    auto last = f.screen->find("badge1");
    REQUIRE(first);
    REQUIRE(last);
    CHECK(last->rect().x0 > first->rect().x0);
    CHECK(last->rect().width() == doctest::Approx(front_screen::kBadgeSize));
}

TEST_CASE("names show beside the buttons once they arrive, and go with the menu")
{
    Fixture f;
    f.desktop.update({ }, { 1.0f / 30, nullptr });
    CHECK(f.screen->label(0).empty());
    settle(f.desktop);
    CHECK(f.screen->label(0) == u"Ride");
    CHECK(f.screen->label(2) == u"Settings");
    CHECK(f.screen->label_alpha(0) == doctest::Approx(1));

    // The buttons have flown to the left edge.
    const Rect r = f.screen->button(FrontMenu::Main, 0)->rect();
    CHECK(r.x1 < 200);

    f.screen->open(FrontMenu::Tools);
    CHECK(f.screen->label(0).empty());
    CHECK(f.screen->label_alpha(0) == 0);
    settle(f.desktop);
    // The main menu has moved out of the way and is no longer shown.
    CHECK(f.screen->button(FrontMenu::Main, 0)->rect().x1 < 0);
}

TEST_CASE("the help lines show what the pointer is on")
{
    Fixture f;
    settle(f.desktop);
    const Rect r = f.screen->button(FrontMenu::Main, 0)->rect();
    const Point centre { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
    f.desktop.update({ centre, false, true }, { 1.0f / 30, nullptr });
    CHECK(f.screen->help_title() == u"Ride");
    CHECK(f.screen->help_text() == u"Go riding");
    f.desktop.update({ { 900, 100 }, false, true }, { 1.0f / 30, nullptr });
    CHECK(f.screen->help_title().empty());
}

TEST_CASE("expansions count as installed when the strings name them")
{
    StringTable strings;
    strings.add("AOP_Name_1", u"Second pack");
    CHECK(named_expansions(strings) == std::array { false, true });
}

TEST_CASE("a settled screen shows its buttons and labels at once")
{
    StringTable strings;
    strings.add("FrontScreen_Play_TT", u"Play");
    Desktop desktop;
    auto& screen = desktop.root().add<FrontScreen>("front", strings, std::array { false, false }, "English");
    desktop.layout({ 0, 0, 1024, 768 });
    screen.settle();
    desktop.update({ });
    CHECK(screen.label_alpha(0) == 1);
}
