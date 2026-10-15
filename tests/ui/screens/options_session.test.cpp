// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/options_session.hpp"

#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        StringTable strings;
        config::GameOptions options;
        Desktop desktop;
        OptionsSession session { desktop, game_skins::options_dialog_style(), strings, options, OptionsChoices { } };

        Fixture()
        {
            desktop.layout({ 0, 0, 1024, 768 });
        }
        void step()
        {
            desktop.update({ });
            session.update();
        }
    };
} // namespace

TEST_CASE("the window opens once and its closing is told once")
{
    Fixture f;
    CHECK(f.session.window() == nullptr);
    f.session.open();
    f.session.open();
    f.step();
    REQUIRE(f.session.window() != nullptr);
    CHECK(f.desktop.root().children().size() == 1);
    CHECK_FALSE(f.session.take_closed());
    f.session.close();
    CHECK(f.session.window() == nullptr);
    CHECK(f.session.take_closed());
    CHECK_FALSE(f.session.take_closed());
}

TEST_CASE("keys picked show in the next window")
{
    Fixture f;
    f.session.open();
    f.step();
    OptionsDialog* dialog = f.session.window();
    REQUIRE(dialog != nullptr);
    dialog->show_page(OptionsPage::Keys);
    auto* usual = dynamic_cast<Button*>(dialog->find("content/page1/usual_keys"));
    REQUIRE(usual != nullptr);
    f.session.choices().keys.bind(Command::Copy, { Key::F3 }); // As if set before
    usual->click();
    f.step();
    const auto keys = f.session.take_keys();
    REQUIRE(keys);
    CHECK(f.session.choices().keys == *keys);
    CHECK_FALSE(f.session.take_keys());
}
