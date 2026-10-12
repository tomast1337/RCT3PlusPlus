// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/edit_box.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    // Every glyph is 10 units wide; lines are 20 tall.
    Font test_font()
    {
        ovl::Font f;
        f.metrics = { 0.1f, 1.5f, 0.5f }; // Lines 20 apart, the baseline 15 down
        f.has_code_table = true;
        for (char16_t c : std::u16string_view(u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "))
            f.glyphs.push_back({ c, 1, 0, { 0, 0, 0.1f, 0.1f }, { 0, 0 }, 0 });
        const std::array<Font::Page, 1> pages = { { { 2, 100, 100 } } };
        return Font::from(f, pages);
    }

    KeyPress key(Key k, bool shift = false, bool control = false)
    {
        return { k, shift, control };
    }

    void type(EditBox& box, std::u16string_view text)
    {
        for (char16_t c : text)
            box.on_key({ Key::Character, false, false, c });
    }
} // namespace

TEST_CASE("typing goes in at the caret")
{
    EditBox box("box", u"park");
    CHECK(box.caret() == 4);
    type(box, u"s");
    CHECK(box.text == u"parks");
    box.set_caret(0);
    type(box, u"my ");
    CHECK(box.text == u"my parks");
    CHECK(box.caret() == 3);
    CHECK(box.take_change());
}

TEST_CASE("the caret moves by characters, words and to the ends")
{
    EditBox box("box", u"big  wooden coaster");
    box.on_key(key(Key::Home));
    CHECK(box.caret() == 0);
    box.on_key(key(Key::Right));
    CHECK(box.caret() == 1);
    box.on_key(key(Key::Right, false, true));
    CHECK(box.caret() == 3);
    box.on_key(key(Key::Right, false, true));
    CHECK(box.caret() == 11);
    box.on_key(key(Key::Left, false, true));
    CHECK(box.caret() == 5);
    box.on_key(key(Key::End));
    CHECK(box.caret() == 19);
    CHECK_FALSE(box.has_selection());
}

TEST_CASE("shift selects, and typing or deleting replaces the selection")
{
    EditBox box("box", u"ride one");
    box.on_key(key(Key::Left, true, true));
    CHECK(box.has_selection());
    CHECK(box.anchor() == 8);
    CHECK(box.caret() == 5);
    type(box, u"two");
    CHECK(box.text == u"ride two");

    box.on_key({ Key::Shortcut, false, true, U'a' });
    CHECK(box.anchor() == 0);
    CHECK(box.caret() == 8);
    box.on_key(key(Key::Delete));
    CHECK(box.text.empty());

    SUBCASE("left without shift collapses a selection to its start")
    {
        box.text = u"abc";
        box.select_all();
        box.on_key(key(Key::Left));
        CHECK(box.caret() == 0);
        CHECK_FALSE(box.has_selection());
    }
}

TEST_CASE("backspace and delete remove a character, with control a word")
{
    EditBox box("box", u"log flume ride");
    box.on_key(key(Key::Backspace));
    CHECK(box.text == u"log flume rid");
    box.on_key(key(Key::Backspace, false, true));
    CHECK(box.text == u"log flume ");
    box.on_key(key(Key::Home));
    box.on_key(key(Key::Delete));
    CHECK(box.text == u"og flume ");
    box.on_key(key(Key::Delete, false, true));
    CHECK(box.text == u" flume ");
    box.on_key(key(Key::Home));
    CHECK(box.take_change());
    box.on_key(key(Key::Backspace)); // Nothing before the caret
    CHECK(box.text == u" flume ");
}

TEST_CASE("surrogate pairs are never split")
{
    EditBox box("box", u"a\U0001F3A2b"); // A roller coaster between two letters
    REQUIRE(box.text.size() == 4);
    box.set_caret(2); // Inside the pair: snapped back
    CHECK(box.caret() == 1);
    box.on_key(key(Key::Right));
    CHECK(box.caret() == 3);
    box.on_key(key(Key::Backspace));
    CHECK(box.text == u"ab");
    box.on_key({ Key::Character, false, false, U'\U0001F3A2' });
    CHECK(box.text == u"a\U0001F3A2b");
}

TEST_CASE("typing is limited by length, allowed characters and case")
{
    EditBox box("box");
    box.max_length = 3;
    type(box, u"abcd");
    CHECK(box.text == u"abc");

    EditBox digits("digits");
    digits.allowed = u"0123456789";
    type(digits, u"1a2 3");
    CHECK(digits.text == u"123");

    EditBox upper("upper");
    upper.upper_case = true;
    type(upper, u"aZ1");
    CHECK(upper.text == u"AZ1");

    EditBox controls("controls");
    for (char32_t c : { U'\t', U'\n', U'\x7f', U'\xD800' })
        controls.on_key({ Key::Character, false, false, c });
    CHECK(controls.text.empty());
}

TEST_CASE("enter commits; escape, tab, up and down go to the parents")
{
    EditBox box("box", u"x");
    CHECK(box.on_key(key(Key::Enter)));
    CHECK(box.take_commit());
    CHECK_FALSE(box.take_commit());
    for (Key k : { Key::Escape, Key::Tab, Key::Up, Key::Down })
        CHECK_FALSE(box.on_key(key(k)));
    CHECK_FALSE(box.on_key({ Key::Shortcut, false, true, U'q' }));
}

TEST_CASE("the pointer places the caret and drags a selection")
{
    FontSet fonts;
    fonts.set(FontRole::Small, test_font());
    Desktop desktop;
    EditBox& box = desktop.root().add<EditBox>("box", u"abcdef");
    box.placement = top_left(0, 0, 200, 24);
    desktop.layout({ 0, 0, 300, 100 });
    const UpdateContext context { 0, &fonts };
    const float x0 = box.padding.x0;

    desktop.update({ { x0 + 21, 10 }, true, true }, context); // Nearer the end of b than the middle of c
    CHECK(desktop.keyboard_focus() == &box);
    CHECK(box.caret() == 2);
    desktop.update({ { x0 + 44, 10 }, true, true }, context);
    CHECK(box.anchor() == 2);
    CHECK(box.caret() == 4);
    desktop.update({ { x0 + 44, 10 }, false, true }, context);
    CHECK(box.has_selection());

    SUBCASE("a new press starts again")
    {
        desktop.update({ { x0 + 60, 10 }, true, true }, context);
        CHECK(box.caret() == 6);
        CHECK_FALSE(box.has_selection());
    }
}

TEST_CASE("long text scrolls to keep the caret in view")
{
    FontSet fonts;
    fonts.set(FontRole::Small, test_font());
    Desktop desktop;
    EditBox& box = desktop.root().add<EditBox>("box", u"abcdefghijklmnopqrst"); // 200 wide
    box.placement = top_left(0, 0, 56, 24);                                     // 50 for text
    desktop.layout({ 0, 0, 300, 100 });
    const UpdateContext context { 0, &fonts };
    desktop.set_keyboard_focus(&box);

    DrawList list;
    const SkinLibrary skins;
    const auto caret_x = [&] {
        list.clear();
        DrawContext draw { list, skins, fonts };
        box.draw(draw);
        // The caret is the last quad: two units wide.
        const auto v = list.vertices();
        REQUIRE(v.size() >= 4);
        return (v[v.size() - 4].x + v[v.size() - 2].x) / 2;
    };
    desktop.update({ }, context);
    CHECK(caret_x() <= 56);
    CHECK(caret_x() >= 3);

    KeyboardState home;
    home.presses.push_back({ Key::Home });
    desktop.update({ }, home, context);
    CHECK(caret_x() == doctest::Approx(3 + 1));
}

TEST_CASE("copy and cut hand the selection to the clipboard")
{
    EditBox box("box", u"big wheel");
    box.on_key(key(Key::Copy));
    CHECK_FALSE(box.take_copied()); // Nothing selected

    box.on_key(key(Key::Left, true, true));
    box.on_key(key(Key::Copy));
    CHECK(box.take_copied() == u"wheel");
    CHECK_FALSE(box.take_copied());
    CHECK(box.text == u"big wheel");

    box.on_key(key(Key::Cut));
    CHECK(box.take_copied() == u"wheel");
    CHECK(box.text == u"big ");
    CHECK(box.take_change());
}

TEST_CASE("paste goes in like typing")
{
    const auto paste = [](EditBox& box, std::u16string text) {
        KeyPress p { Key::Paste };
        p.text = std::move(text);
        return box.on_key(p);
    };
    EditBox box("box", u"ride");
    box.select_all();
    CHECK(paste(box, u"log\r\nflume"));
    CHECK(box.text == u"log flume");
    CHECK(box.caret() == 9);

    SUBCASE("cut to the length, by whole characters")
    {
        EditBox short_box("short", u"ab");
        short_box.max_length = 4;
        paste(short_box, u"c\U0001F3A2d");
        CHECK(short_box.text == u"abc\U0001F3A2");
    }
    SUBCASE("only allowed characters")
    {
        EditBox digits("digits");
        digits.allowed = u"0123456789";
        paste(digits, u"$1,250");
        CHECK(digits.text == u"1250");
    }
    SUBCASE("an empty paste over a selection deletes it")
    {
        box.select_all();
        paste(box, u"");
        CHECK(box.text.empty());
    }
}

TEST_CASE("the desktop collects what a focused widget copied")
{
    Desktop desktop;
    EditBox& box = desktop.root().add<EditBox>("box", u"coaster");
    desktop.layout({ 0, 0, 300, 100 });
    desktop.set_keyboard_focus(&box);
    box.select_all();

    KeyboardState keys;
    keys.presses.push_back({ Key::Copy, false, true });
    desktop.update({ }, keys);
    CHECK(desktop.take_clipboard() == u"coaster");
    CHECK_FALSE(desktop.take_clipboard());
}
