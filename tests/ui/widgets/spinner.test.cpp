// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/spinner.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    // Arrows 20 wide on the right of a 100 by 20 spinner: up from y 0 to 10, down from 10 to 20.
    SpinnerStyle style()
    {
        SpinnerStyle s;
        s.arrow_width = 20;
        s.repeat_delay = 0.5f;
        s.repeat_interval = 0.1f;
        return s;
    }

    struct Fixture
    {
        Desktop desktop;
        Spinner& spinner = desktop.root().add<Spinner>("spinner", style());

        Fixture()
        {
            spinner.placement = top_left(0, 0, 100, 20);
            spinner.minimum = -5;
            spinner.maximum = 20;
            spinner.step = 2;
            desktop.layout({ 0, 0, 300, 300 });
            desktop.update({ { 200, 200 }, false, true });
        }

        void keys(std::vector<KeyPress> presses)
        {
            KeyboardState k;
            k.presses = std::move(presses);
            desktop.update({ { 200, 200 }, false, true }, k);
        }
    };
} // namespace

TEST_CASE("the arrows step the value and repeat while held")
{
    Fixture f;
    CHECK(f.spinner.field().text == u"0");
    f.desktop.update({ { 90, 5 }, true, true });
    CHECK(f.spinner.value() == 2);
    CHECK(f.spinner.field().text == u"2");
    CHECK(f.spinner.take_change());
    f.desktop.update({ { 90, 5 }, true, true }, { 0.3f });
    CHECK(f.spinner.value() == 2);
    f.desktop.update({ { 90, 5 }, true, true }, { 0.25f }); // 0.55 s held: one repeat
    CHECK(f.spinner.value() == 4);
    f.desktop.update({ { 90, 5 }, false, true }, { 1.0f });
    CHECK(f.spinner.value() == 4);

    f.desktop.update({ { 90, 15 }, true, true });
    CHECK(f.spinner.value() == 2);
}

TEST_CASE("the value stays in range")
{
    Fixture f;
    f.spinner.set_value(19);
    f.desktop.update({ { 90, 5 }, true, true });
    CHECK(f.spinner.value() == 20);
    f.desktop.update({ { 90, 5 }, false, true });
    f.desktop.update({ { 90, 5 }, true, true });
    CHECK(f.spinner.value() == 20);
    CHECK(f.spinner.take_change());
    CHECK_FALSE(f.spinner.take_change());
    f.spinner.set_value(-100);
    CHECK(f.spinner.value() == -5);
}

TEST_CASE("a typed number is read back on enter or when the focus leaves")
{
    Fixture f;
    f.desktop.set_keyboard_focus(&f.spinner.field());
    f.keys({ { Key::Backspace }, { Key::Character, false, false, U'1' }, { Key::Character, false, false, U'5' }, { Key::Enter } });
    f.desktop.update({ { 200, 200 }, false, true });
    CHECK(f.spinner.value() == 15);
    CHECK(f.spinner.take_change());

    SUBCASE("out of range is kept in range")
    {
        f.keys({ { Key::Character, false, false, U'9' }, { Key::Character, false, false, U'9' }, { Key::Enter } });
        f.desktop.update({ { 200, 200 }, false, true });
        CHECK(f.spinner.value() == 20);
        CHECK(f.spinner.field().text == u"20");
    }
    SUBCASE("not a number puts the old value back")
    {
        f.keys({ { Key::Character, false, false, U'-' }, { Key::Character, false, false, U'-' } });
        f.desktop.set_keyboard_focus(nullptr);
        f.desktop.update({ { 200, 200 }, false, true });
        f.desktop.update({ { 200, 200 }, false, true });
        CHECK(f.spinner.value() == 15);
        CHECK(f.spinner.field().text == u"15");
    }
    SUBCASE("up and down step from what was typed")
    {
        f.keys({ { Key::Backspace }, { Key::Backspace }, { Key::Character, false, false, U'7' }, { Key::Up } });
        CHECK(f.spinner.value() == 9);
        f.keys({ { Key::PageDown } });
        CHECK(f.spinner.value() == -5);
    }
}

TEST_CASE("the wheel over the spinner steps it")
{
    Fixture f;
    f.desktop.update({ { 30, 10 }, false, true, { 0, 2 } });
    CHECK(f.spinner.value() == 4);
}

TEST_CASE("a spinner with decimals shows a point and reads one back")
{
    Fixture f;
    f.spinner.maximum = 2000;
    f.spinner.decimals = 2;
    f.spinner.set_value(5);
    CHECK(f.spinner.field().text == u"0.05");
    f.spinner.set_value(150);
    CHECK(f.spinner.field().text == u"1.50");
    f.desktop.set_keyboard_focus(&f.spinner.field());
    const auto type = [&](std::u32string_view text) {
        std::vector<KeyPress> presses(4, { Key::Backspace });
        for (const char32_t c : text)
            presses.push_back({ Key::Character, false, false, c });
        presses.push_back({ Key::Enter });
        f.keys(std::move(presses));
    };
    type(U"2.5");
    CHECK(f.spinner.value() == 250);
    type(U"3");
    CHECK(f.spinner.value() == 300);
    type(U"1.234"); // More places than it has: the old value stays
    CHECK(f.spinner.value() == 300);
    CHECK(f.spinner.field().text == u"3.00");
}

TEST_CASE("a spinner with a unit shows it after the number and reads the number with or without it")
{
    Fixture f;
    f.spinner.unit = u" mph";
    f.spinner.set_value(7);
    CHECK(f.spinner.field().text == u"7 mph");
    f.desktop.set_keyboard_focus(&f.spinner.field());
    // Backspace takes the unit's last letters, leaving "7 m": not a number, so 7 stays.
    f.keys({ { Key::Backspace }, { Key::Backspace }, { Key::Enter } });
    CHECK(f.spinner.value() == 7);
    CHECK(f.spinner.field().text == u"7 mph");
    std::vector<KeyPress> presses(5, { Key::Backspace });
    presses.push_back({ Key::Character, false, false, U'9' });
    presses.push_back({ Key::Enter });
    f.keys(std::move(presses));
    CHECK(f.spinner.value() == 9);
    CHECK(f.spinner.field().text == u"9 mph");
}
