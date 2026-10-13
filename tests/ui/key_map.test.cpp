// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/key_map.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    KeyboardState pressed(std::initializer_list<KeyPress> presses)
    {
        KeyboardState keyboard;
        keyboard.presses = presses;
        return keyboard;
    }

    bool same(const KeyPress& a, const KeyPress& b)
    {
        return a.key == b.key && a.shift == b.shift && a.control == b.control && a.character == b.character;
    }
} // namespace

TEST_CASE("the table follows the commands")
{
    const auto table = commands_table();
    REQUIRE(table.size() == kCommandCount);
    for (std::size_t i = 0; i < table.size(); ++i)
        CHECK(static_cast<std::size_t>(table[i].command) == i);
}

TEST_CASE("the usual keys arrive as the presses the widgets know")
{
    const KeyMap map;
    const auto out = map.translate(pressed(
        {
            { Key::PageDown, false, true },
            { Key::Shortcut, false, true, U'c' },
            { Key::Delete, true, false },
            { Key::Character, false, false, U'q' },
            { Key::F12 },
        }));
    REQUIRE(out.keyboard.presses.size() == 4);
    CHECK(same(out.keyboard.presses[0], { Key::Tab, false, true }));
    CHECK(same(out.keyboard.presses[1], { Key::Copy }));
    CHECK(same(out.keyboard.presses[2], { Key::Cut }));
    CHECK(same(out.keyboard.presses[3], { Key::Character, false, false, U'q' }));
    CHECK(out.commands == std::vector { Command::DeveloperWindows });
}

TEST_CASE("a command's usual keys do nothing once it is moved")
{
    KeyMap map;
    map.bind(Command::Cancel, { Key::F4 });
    const auto out = map.translate(pressed({ { Key::Escape }, { Key::F4 }, { Key::Left, false, true } }));
    REQUIRE(out.keyboard.presses.size() == 2);
    CHECK(same(out.keyboard.presses[0], { Key::Escape }));
    CHECK(same(out.keyboard.presses[1], { Key::Left, false, true })); // Not a command
}

TEST_CASE("binding keys takes them from other commands")
{
    KeyMap map;
    map.bind(Command::DefaultButton, { Key::Escape });
    CHECK(map.keys(Command::Cancel).empty());
    CHECK(map.command_of({ Key::Escape }) == Command::DefaultButton);
    const auto out = map.translate(pressed({ { Key::Escape }, { Key::Enter } }));
    REQUIRE(out.keyboard.presses.size() == 1);
    CHECK(same(out.keyboard.presses[0], { Key::Enter }));
    map.reset();
    CHECK(map == KeyMap { });
}

TEST_CASE("the file keeps only what differs from the usual keys")
{
    CHECK(KeyMap { }.text().empty());
    KeyMap map;
    map.set_keys(Command::NextPage, { { Key::F6 }, { Key::Shortcut, U'n', true, false } });
    map.set_keys(Command::DeveloperWindows, { });
    const std::string text = map.text();
    CHECK(text == "next_page = F6, Ctrl+N\ndeveloper_windows =\n");
    CHECK(KeyMap::parse(text) == map);
}

TEST_CASE("the file reports what it cannot read and keeps the rest")
{
    std::vector<KeyMapProblem> problems;
    const KeyMap map = KeyMap::parse("# keys\ncancel = F4, Space\nnothing = F5\nno equals\n", &problems);
    CHECK(map.keys(Command::Cancel) == std::vector { KeyCombo { Key::F4 } });
    REQUIRE(problems.size() == 3);
    CHECK(problems[0].line == 2);
    CHECK(problems[1].line == 3);
    CHECK(problems[2].line == 4);
}
