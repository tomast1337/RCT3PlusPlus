// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tutorial_picker.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        StringTable strings;
        Desktop desktop;
        TutorialPicker* menu = nullptr;

        Fixture()
        {
            strings.add("FrontScreen_StartTutorial_TT", u"Go");
            strings.add("FrontScreen_StartTutorial_Help", u"Begin it");
            std::vector<TutorialInfo> list(3);
            list[0].name = u"Looking";
            list[1].name = u"Building";
            list[2].name = u"Selling";
            menu = &desktop.root().add<TutorialPicker>("tutorials", game_skins::tutorial_picker_style(), strings, std::move(list));
            desktop.layout({ 0, 0, 1024, 768 });
            tick();
        }
        void tick()
        {
            desktop.update({ });
        }
        void hover(const Widget& w)
        {
            const Rect r = w.rect();
            desktop.update({ { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 } });
        }
        void press(FrontButton& b)
        {
            b.click();
            tick();
        }
    };
} // namespace

TEST_CASE("the tutorials sit on an arc from the top, the start button low on the right")
{
    Fixture f;
    const Rect first = f.menu->button(0).rect();
    const Rect second = f.menu->button(1).rect();
    const Rect start = f.menu->start_button().rect();
    CHECK(first.width() == doctest::Approx(tutorial_picker::kButtonSize));
    CHECK(second.y0 > first.y0);
    CHECK(second.x0 < first.x0);
    CHECK(start.x0 > first.x1);
    CHECK(start.y0 > first.y1);
}

TEST_CASE("the first tutorial is picked; a click picks another and start takes it")
{
    Fixture f;
    CHECK(f.menu->picked() == 0);
    f.press(f.menu->button(2));
    CHECK(f.menu->picked() == 2);
    CHECK_FALSE(f.menu->take_start());
    f.press(f.menu->start_button());
    CHECK(f.menu->take_start() == 2);
    CHECK_FALSE(f.menu->take_start());
}

TEST_CASE("a tutorial whose park is missing cannot be picked or started")
{
    Fixture f;
    f.menu->set_available(1, false);
    f.press(f.menu->button(1));
    CHECK(f.menu->picked() == 0);
    f.menu->set_available(0, false);
    f.tick();
    CHECK_FALSE(f.menu->start_button().enabled);
    f.press(f.menu->start_button());
    CHECK_FALSE(f.menu->take_start());
}

TEST_CASE("the bottom strip names what the pointer is over")
{
    Fixture f;
    CHECK_FALSE(f.menu->help());
    f.hover(f.menu->button(1));
    REQUIRE(f.menu->help());
    CHECK((*f.menu->help())[0] == u"Building");
    CHECK((*f.menu->help())[1].empty());
    f.hover(f.menu->start_button());
    REQUIRE(f.menu->help());
    CHECK((*f.menu->help())[0] == u"Go");
    CHECK((*f.menu->help())[1] == u"Begin it");
}
