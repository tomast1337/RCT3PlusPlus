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
            box = &desktop.root().add<KeyBox>("keys", KeyCombo { Key::F12 });
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
    CHECK(f.box->combo == KeyCombo { Key::Shortcut, U'd', true, true });
    CHECK(f.box->take_change());
    CHECK_FALSE(f.box->take_change());
}

TEST_CASE("escape and losing the focus keep the old combination")
{
    Fixture f;
    f.press({ Key::Space });
    f.press({ Key::Escape });
    CHECK_FALSE(f.box->waiting());
    CHECK(f.box->combo == KeyCombo { Key::F12 });

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
