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
            dialog = &desktop.root().add<OptionsDialog>("options", game_skins::options_dialog_style(), strings, options, std::move(choices));
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
    REQUIRE(f.dialog->find("content/caption") != nullptr);
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

TEST_CASE("the custom graphics settings show only with the custom quality")
{
    Fixture f;
    f.options.set(O::GraphicsQuality, 2);
    f.desktop.update({ });
    CHECK_FALSE(f.dialog->custom_graphics().visible);
    f.options.set(O::GraphicsQuality, config::kCustomQuality);
    f.desktop.update({ });
    CHECK(f.dialog->custom_graphics().visible);
}

TEST_CASE("the language list shows with two languages, each by its own name")
{
    Fixture one({ { "English" }, "English", { }, std::nullopt, { } });
    CHECK(one.dialog->language_list() == nullptr);

    Fixture two({ { "English", "Dutch" }, "English", { }, std::nullopt, { } });
    Dropdown* list = two.dialog->language_list();
    REQUIRE(list != nullptr);
    CHECK(list->items == std::vector<std::u16string> { u"English", u"Nederlands" });
    CHECK(list->selected() == 0);
    list->on_key({ Key::Down });
    two.desktop.update({ });
    CHECK(two.dialog->take_language() == "Dutch");
    CHECK_FALSE(two.dialog->take_language());
}

TEST_CASE("controls change the options at once")
{
    Fixture f;
    // The first control on the general page is the full screen tick box.
    Widget* page = f.dialog->find("content/page0");
    REQUIRE(page != nullptr);
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
        for (const auto& c : f.dialog->find("content/page1")->children())
        {
            if (auto* box = dynamic_cast<KeyBox*>(c.get()))
                boxes.push_back(box);
        }
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
    auto* usual = dynamic_cast<Button*>(f.dialog->find("content/page1/usual_keys"));
    REQUIRE(usual != nullptr);
    usual->click();
    f.desktop.update({ });
    CHECK(f.dialog->take_keys() == KeyMap { });
    CHECK(key_boxes(f)[static_cast<std::size_t>(Command::Copy)]->combos.size() == 2);
}
