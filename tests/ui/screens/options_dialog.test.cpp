// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/options_dialog.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;
using O = config::GameOption;

namespace
{
    struct Fixture
    {
        StringTable strings;
        config::GameOptions options;
        Desktop desktop;
        OptionsDialog* dialog = nullptr;

        explicit Fixture(OptionsChoices choices = { })
        {
            strings.add("GameOptionsDialog_GameSettingsTabHelp", u"General");
            strings.add("GameOptionsDialog_GraphicSettingsTabHelp", u"Pictures");
            strings.add("Language_Dutch", u"Nederlands");
            dialog = &desktop.root().add<OptionsDialog>("options", game_skins::options_dialog_style(kGamePalette), strings, options, std::move(choices));
            desktop.layout({ 0, 0, 1024, 768 });
            desktop.update({ });
        }
    };
} // namespace

TEST_CASE("the tabs pick the page, its name showing beside them")
{
    Fixture f;
    CHECK(f.dialog->page() == OptionsPage::General);
    CHECK(f.dialog->tab(OptionsPage::General).on);
    REQUIRE(f.dialog->find("content/caption"));
    f.dialog->tab(OptionsPage::Graphics).click();
    f.desktop.update({ });
    CHECK(f.dialog->page() == OptionsPage::Graphics);
    CHECK_FALSE(f.dialog->tab(OptionsPage::General).on);
    CHECK(f.dialog->tab(OptionsPage::Graphics).on);

    // Clicking the open tab leaves it open.
    f.dialog->tab(OptionsPage::Graphics).click();
    f.desktop.update({ });
    CHECK(f.dialog->tab(OptionsPage::Graphics).on);
}

TEST_CASE("the custom graphics settings always show, usable only with the custom quality")
{
    Fixture f;
    const auto usable = [&] {
        for (const auto& c : f.dialog->custom_graphics().content().children())
        {
            if (auto* tick = dynamic_cast<Tickbox*>(c.get()); tick != nullptr && tick->visible)
                return tick->enabled;
        }
        FAIL("no tick box");
        return false;
    };
    f.options.set(O::GraphicsQuality, 2);
    f.desktop.update({ });
    CHECK(f.dialog->custom_graphics().visible);
    CHECK_FALSE(usable());
    f.options.set(O::GraphicsQuality, config::kCustomQuality);
    f.desktop.update({ });
    CHECK(usable());
}

TEST_CASE("the scenic page sets the surroundings whatever the quality level")
{
    Fixture f;
    f.options.set(O::GraphicsQuality, 2); // Not custom
    f.dialog->tab(OptionsPage::Scenic).click();
    f.desktop.update({ });
    CHECK(f.dialog->page() == OptionsPage::Scenic);
    auto page = f.dialog->find("content/GameOptionsDialog_ScenicSettingsTabHelp");
    REQUIRE(page);
    const bool sky = f.options.flag(O::Sky);
    const bool clouds = f.options.flag(O::Clouds);
    const bool trees = f.options.flag(O::Trees3D);
    for (const auto& c : page->children())
    {
        if (auto* tick = dynamic_cast<Tickbox*>(c.get()); tick != nullptr && tick->visible)
            tick->on_key({ Key::Space });
    }
    f.desktop.update({ });
    CHECK(f.options.flag(O::Sky) != sky);
    CHECK(f.options.flag(O::Clouds) != clouds);
    CHECK(f.options.flag(O::Trees3D) != trees);
    CHECK_FALSE(f.options.flag(O::Fullscreen));
}

TEST_CASE("the language list shows with two languages, each by its own name")
{
    Fixture one({ { "English" }, "English", { }, std::nullopt, { } });
    CHECK(!one.dialog->language_list());

    Fixture two({ { "English", "Dutch" }, "English", { }, std::nullopt, { } });
    auto list = two.dialog->language_list();
    REQUIRE(list);
    CHECK(list->items == std::vector<std::u16string> { u"English", u"Nederlands" });
    CHECK(list->selected() == 0);
    list->on_key({ Key::Down });
    two.desktop.update({ });
    CHECK(two.dialog->take_language() == "Dutch");
    CHECK_FALSE(two.dialog->take_language());
}

TEST_CASE("the park's music is picked from the files, shuffled or none, and kept in the options")
{
    Fixture none;
    CHECK(!none.dialog->background_music_list());

    OptionsChoices choices;
    choices.background_music = { "A.wma", "B.wma" };
    choices.background_track = 1;
    Fixture f(choices);
    auto list = f.dialog->background_music_list();
    REQUIRE(list);
    CHECK(list->items.size() == 4); // The files, shuffle, no music
    CHECK(list->selected() == 1);   // Not chosen yet: the owner's pick

    list->on_key({ Key::Down });
    f.desktop.update({ });
    CHECK(f.options.flag(O::ShuffleBackgroundMusic));
    CHECK(f.dialog->take_change());
    list->on_key({ Key::Down });
    f.desktop.update({ });
    CHECK_FALSE(f.options.flag(O::ShuffleBackgroundMusic));
    CHECK(f.options.background_music() == "");
    list->on_key({ Key::Home });
    f.desktop.update({ });
    CHECK(f.options.background_music() == "A.wma");
}

TEST_CASE("controls change the options at once")
{
    Fixture f;
    // The first tick box on the graphics page is full screen.
    auto page = f.dialog->find("content/GameOptionsDialog_GraphicSettingsTabHelp");
    REQUIRE(page);
    Tickbox* fullscreen = nullptr;
    for (const auto& c : page->children())
    {
        if (fullscreen == nullptr)
            fullscreen = dynamic_cast<Tickbox*>(c.get());
    }
    REQUIRE(fullscreen != nullptr);
    fullscreen->on_key({ Key::Space });
    f.desktop.update({ });
    CHECK(f.options.flag(O::Fullscreen));
    CHECK(f.dialog->take_change());
    CHECK_FALSE(f.dialog->take_change());
}

namespace
{
    std::vector<KeyBox*> key_boxes(const Fixture& f)
    {
        std::vector<KeyBox*> boxes;
        for (std::size_t i = 0; i < kCommandCount; ++i)
            boxes.push_back(&f.dialog->key_box(static_cast<Command>(i)));
        return boxes;
    }

    void press(Fixture& f, KeyPress key)
    {
        KeyboardState keys;
        keys.presses.push_back(std::move(key));
        f.desktop.update({ }, keys);
    }
} // namespace

TEST_CASE("the keys page sets each command's keys")
{
    Fixture f;
    f.dialog->show_page(OptionsPage::Keys);
    const auto boxes = key_boxes(f);
    REQUIRE(boxes.size() == kCommandCount);
    KeyBox& developer = *boxes[static_cast<std::size_t>(Command::DeveloperWindows)];
    CHECK(developer.combos == std::vector { KeyCombo { Key::F12 } });

    f.desktop.set_keyboard_focus(&developer);
    press(f, { Key::Enter });
    CHECK(f.dialog->waiting_for_keys());
    press(f, { Key::Escape, true }); // Only Escape alone stops waiting
    CHECK_FALSE(f.dialog->waiting_for_keys());
    auto keys = f.dialog->take_keys();
    REQUIRE(keys);
    CHECK(keys->keys(Command::DeveloperWindows) == std::vector { KeyCombo { Key::Escape, 0, false, true } });
    CHECK_FALSE(f.dialog->take_keys());

    // Keys another command had are taken from it, and its box shows that.
    KeyBox& cancel = *boxes[static_cast<std::size_t>(Command::Cancel)];
    f.desktop.set_keyboard_focus(&developer);
    press(f, { Key::Enter });
    press(f, { Key::Enter });
    CHECK(developer.combos == std::vector { KeyCombo { Key::Enter } });
    CHECK(boxes[static_cast<std::size_t>(Command::DefaultButton)]->combos.empty());
    CHECK(cancel.combos == std::vector { KeyCombo { Key::Escape } });
    CHECK(f.dialog->take_keys()->keys(Command::DefaultButton).empty());
}

TEST_CASE("the usual keys button puts every command back")
{
    OptionsChoices choices;
    choices.keys.bind(Command::Copy, { Key::F3 });
    Fixture f(choices);
    f.dialog->show_page(OptionsPage::Keys);
    CHECK(key_boxes(f)[static_cast<std::size_t>(Command::Copy)]->combos == std::vector { KeyCombo { Key::F3 } });
    auto usual = dynamic_cast<Button*>(rct3::as_pointer(f.dialog->find("content/GameOptionsDialog_KeyBindingsHelp/usual_keys")));
    REQUIRE(usual != nullptr);
    usual->click();
    f.desktop.update({ });
    CHECK(f.dialog->take_keys() == KeyMap { });
    CHECK(key_boxes(f)[static_cast<std::size_t>(Command::Copy)]->combos.size() == 2);
}

TEST_CASE("the keys page lists the game's commands in groups, any key binding them")
{
    Fixture f;
    f.strings.add("KeyGroup_Universal", u"Everywhere");
    f.dialog->show_page(OptionsPage::Keys);
    CHECK(!f.dialog->play_key_box(PlayCommand::FlattenTerrain)); // Fixed: only shown
    auto pause = f.dialog->play_key_box(PlayCommand::TogglePause);
    auto reset = f.dialog->play_key_box(PlayCommand::ResetCamera);
    REQUIRE(pause);
    REQUIRE(reset);
    CHECK(pause->game_key);

    // Plain letters and Escape bind; the key leaves the command that had it.
    f.desktop.set_keyboard_focus(rct3::as_pointer(reset));
    press(f, { Key::Enter });
    press(f, { Key::Letter, false, false, U'p' });
    CHECK(reset->combos == std::vector { KeyCombo { Key::Letter, U'p' } });
    CHECK(pause->combos.empty());
    const auto keys = f.dialog->take_keys();
    REQUIRE(keys);
    CHECK(keys->play_key(PlayCommand::ResetCamera) == KeyCombo { Key::Letter, U'p' });
    CHECK_FALSE(keys->play_key(PlayCommand::TogglePause));

    f.desktop.set_keyboard_focus(rct3::as_pointer(pause));
    press(f, { Key::Enter });
    press(f, { Key::Escape });
    CHECK(pause->combos == std::vector { KeyCombo { Key::Escape } });
    CHECK_FALSE(f.dialog->take_keys()->play_key(PlayCommand::ExitGame));
}

TEST_CASE("rows of options nothing acts on show greyed and cannot be used")
{
    OptionsChoices choices;
    choices.inert.set(static_cast<std::size_t>(O::Sky));
    Fixture f(choices);
    f.dialog->show_page(OptionsPage::Scenic);
    f.desktop.update({ });
    auto page = f.dialog->find("content/GameOptionsDialog_ScenicSettingsTabHelp");
    REQUIRE(page);
    int usable = 0;
    int greyed = 0;
    for (const auto& c : page->children())
    {
        if (auto* tick = dynamic_cast<Tickbox*>(c.get()); tick != nullptr && tick->visible)
            ++(tick->enabled ? usable : greyed);
    }
    CHECK(greyed == 1);
    CHECK(usable > 1);
}

TEST_CASE("the language shows but cannot change when locked")
{
    OptionsChoices choices { { "English", "Dutch" }, "English", { }, std::nullopt, { } };
    choices.language_locked = true;
    Fixture f(choices);
    auto list = f.dialog->language_list();
    REQUIRE(list);
    CHECK(list->visible);
    CHECK_FALSE(list->enabled);
}

TEST_CASE("the camera page says what the mouse does in the chosen mode")
{
    Fixture f;
    f.strings.add("GameOptionsDialog_CameraControlsRightMouse", u"Right:");
    f.strings.add("GameOptionsDialog_CameraControlsLeftMouse", u"Left:");
    f.strings.add("GameOptionsDialog_CameraControlsActionMove", u"Move");
    f.strings.add("GameOptionsDialog_CameraControlsActionRotate", u"Rotate");
    // The strings are read when the window is made.
    f.desktop.root().remove(*f.dialog);
    f.dialog = &f.desktop.root().add<OptionsDialog>("options", game_skins::options_dialog_style(kGamePalette), f.strings, f.options, OptionsChoices { });
    f.dialog->show_page(OptionsPage::Camera);
    f.options.set(O::CameraMode, 3); // Normal
    f.desktop.update({ });
    CHECK(f.dialog->camera_help(0).text == u"Left:");
    CHECK(f.dialog->camera_help(1).text == u"Right: Move");
    f.options.set(O::CameraMode, 1); // Freelook
    f.desktop.update({ });
    CHECK(f.dialog->camera_help(1).text == u"Right: Rotate");
}

TEST_CASE("the graphics page's auto detect shows but does nothing")
{
    Fixture f;
    CHECK_FALSE(f.dialog->auto_detect().enabled);
}
