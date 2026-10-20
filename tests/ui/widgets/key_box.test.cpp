// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/key_box.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        Desktop desktop;
        KeyBox* box = nullptr;

        Fixture()
        {
            box = &desktop.root().add<KeyBox>("keys", std::vector { KeyCombo { Key::F12 }, KeyCombo { Key::F11 } });
            box->placement = top_left(10, 10, 100, 20);
            desktop.layout({ 0, 0, 400, 300 });
            desktop.set_keyboard_focus(box);
            desktop.update({ });
        }
        void press(KeyPress key)
        {
            KeyboardState keyboard;
            keyboard.presses.push_back(std::move(key));
            desktop.update({ }, keyboard);
        }
    };
} // namespace

TEST_CASE("the key box takes the next combination that can be bound")
{
    Fixture f;
    f.press({ Key::Enter });
    REQUIRE(f.box->waiting());
    f.press({ Key::Character, false, false, U'd' }); // Types; cannot be bound
    CHECK(f.box->waiting());
    f.press({ Key::Shortcut, true, true, U'd' });
    CHECK_FALSE(f.box->waiting());
    CHECK(f.box->combos == std::vector { KeyCombo { Key::Shortcut, U'd', true, true } });
    CHECK(f.box->take_change());
    CHECK_FALSE(f.box->take_change());
}

TEST_CASE("escape and losing the focus keep the old combination")
{
    Fixture f;
    f.press({ Key::Space });
    f.press({ Key::Escape });
    CHECK_FALSE(f.box->waiting());
    CHECK(f.box->combos.size() == 2);

    f.press({ Key::Space });
    f.desktop.set_keyboard_focus(nullptr);
    f.desktop.update({ });
    CHECK_FALSE(f.box->waiting());
    CHECK_FALSE(f.box->take_change());
}

TEST_CASE("a click starts waiting")
{
    Fixture f;
    f.desktop.update({ { 20, 20 }, true });
    f.desktop.update({ { 20, 20 }, false });
    CHECK(f.box->waiting());
}

TEST_CASE("while waiting the key box takes tab and shifted escape")
{
    Fixture f;
    f.press({ Key::Enter });
    f.press({ Key::Tab, true });
    CHECK(rct3::as_pointer(f.desktop.keyboard_focus()) == f.box);
    CHECK(f.box->combos == std::vector { KeyCombo { Key::Tab, 0, false, true } });
    f.press({ Key::Enter });
    f.press({ Key::Escape, true });
    CHECK(f.box->combos == std::vector { KeyCombo { Key::Escape, 0, false, true } });
}

TEST_CASE("a box cut off entirely leaves the cut as it found it")
{
    rct3::ovl::Font f;
    f.metrics = { 0.1f, 1.0f, 0.2f };
    f.has_code_table = true;
    const std::array<Font::Page, 1> pages = { { { 9, 100, 100 } } };
    FontSet fonts;
    fonts.set(FontRole::Small, Font::from(f, pages));
    Fixture fx;
    DrawList list;
    const SkinLibrary skins;
    DrawContext context { list, skins, fonts };
    REQUIRE(list.push_clip({ 200, 200, 300, 300 })); // Away from the box: nothing of it shows
    fx.box->draw(context);
    REQUIRE(list.clip());
    CHECK(list.clip()->x0 == 200);
    list.pop_clip();
    CHECK(!list.clip());
}
