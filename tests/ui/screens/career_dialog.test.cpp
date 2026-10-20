// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/career_dialog.hpp"

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
        CareerDialog* dialog = nullptr;

        Fixture()
        {
            strings.add("ProfileWindow_Title", u"Pick a run");
            strings.add("ProfileWindow_New_TT", u"Start one");
            strings.add("ProfileWindow_ConfirmDeleteMessage", u"Really?");
            dialog = &desktop.root().add<CareerDialog>("careers", game_skins::career_dialog_style(), strings);
            desktop.layout({ 0, 0, 1024, 768 });
            desktop.update({ });
        }

        void tick(float seconds = 0)
        {
            desktop.update({ }, { }, { seconds, nullptr });
        }

        void answer(bool yes)
        {
            auto q = dialog->question();
            REQUIRE(q);
            (yes ? q->buttons().front() : q->buttons().back())->click();
            tick();
            tick();
        }
    };

    CareerRow row(std::u16string name, std::size_t a, std::size_t e, std::size_t t, std::size_t n)
    {
        CareerRow r;
        r.name = std::move(name);
        r.score.completed = a + e + t;
        r.score.entrepreneur = e + t;
        r.score.tycoon = t;
        r.score.percent = static_cast<float>(a + 2 * e + 3 * t) * 100 / static_cast<float>(3 * n);
        return r;
    }
} // namespace

TEST_CASE("progress is written with three significant digits")
{
    CHECK(career_percent(0) == u"0%");
    CHECK(career_percent(100.0f / 54) == u"1.85%");
    CHECK(career_percent(100.0f / 3) == u"33.3%");
    CHECK(career_percent(100) == u"100%");
}

TEST_CASE("each row counts the levels and shows the progress; the first is picked")
{
    Fixture f;
    CHECK(f.dialog->title == u"Pick a run");
    f.dialog->set_careers({ row(u"First", 2, 1, 1, 9), row(u"Second", 0, 0, 0, 9) });
    CHECK(f.dialog->size() == 2);
    CHECK(f.dialog->selected() == 0);
    CHECK(f.dialog->name_box(0).text == u"First");
    const auto text = f.dialog->row_text(0);
    CHECK(text[0] == u"1");     // Tycoon
    CHECK(text[1] == u"2");     // Entrepreneur or better
    CHECK(text[2] == u"4");     // Any level
    CHECK(text[3] == u"25.9%"); // 7 points of 27
    f.dialog->set_careers({ });
    CHECK_FALSE(f.dialog->selected());
}

TEST_CASE("new asks the owner, whose career joins the list picked")
{
    Fixture f;
    f.dialog->set_careers({ row(u"First", 0, 0, 0, 9) });
    f.dialog->new_button().click();
    f.tick();
    CHECK(f.dialog->take_new());
    CHECK_FALSE(f.dialog->take_new());
    f.dialog->add_career(row(u"Player 2", 0, 0, 0, 9));
    CHECK(f.dialog->size() == 2);
    CHECK(f.dialog->selected() == 1);
    CHECK_FALSE(f.dialog->closing());
}

TEST_CASE("load takes the picked career, by button or a double click, with its new name")
{
    Fixture f;
    f.dialog->set_careers({ row(u"First", 0, 0, 0, 9), row(u"Second", 0, 0, 0, 9) });
    f.dialog->name_box(0).text = u"Renamed";
    f.dialog->load_button().click();
    f.tick();
    const auto rename = f.dialog->take_rename();
    REQUIRE(rename);
    CHECK(rename->first == 0);
    CHECK(rename->second == u"Renamed");
    CHECK(f.dialog->take_load() == 0);

    f.dialog->click_row(1);
    f.tick(0.1f);
    CHECK(f.dialog->selected() == 1);
    CHECK_FALSE(f.dialog->take_load());
    f.dialog->click_row(1);
    f.tick(0.1f);
    CHECK(f.dialog->take_load() == 1);
    CHECK_FALSE(f.dialog->take_rename());
}

TEST_CASE("renaming on Enter; an empty name puts the old one back")
{
    Fixture f;
    f.dialog->set_careers({ row(u"First", 0, 0, 0, 9) });
    EditBox& box = f.dialog->name_box(0);
    box.text.clear();
    box.on_key({ Key::Enter });
    f.tick();
    CHECK(box.text == u"First");
    CHECK_FALSE(f.dialog->take_rename());
    box.text = u"Mine";
    box.on_key({ Key::Enter });
    f.tick();
    CHECK(f.dialog->take_rename() == std::pair<std::size_t, std::u16string> { 0, u"Mine" });
}

TEST_CASE("delete asks, then takes the row out and leaves nothing picked")
{
    Fixture f;
    f.dialog->set_careers({ row(u"First", 0, 0, 0, 9), row(u"Second", 0, 0, 0, 9) });
    f.dialog->select(1);
    f.dialog->delete_button().click();
    f.tick();
    REQUIRE(f.dialog->question());
    CHECK(f.dialog->question()->text().text == u"Really?");
    f.answer(false);
    CHECK(f.dialog->size() == 2);
    CHECK_FALSE(f.dialog->take_delete());

    f.dialog->delete_button().click();
    f.tick();
    f.answer(true);
    CHECK(f.dialog->take_delete() == 1);
    CHECK(f.dialog->size() == 1);
    CHECK_FALSE(f.dialog->selected());
    f.dialog->delete_button().click(); // Nothing picked: nothing asked
    f.tick();
    CHECK(!f.dialog->question());
}

TEST_CASE("the hovered button's name shows beside the buttons")
{
    Fixture f;
    const Rect r = f.dialog->new_button().rect();
    f.desktop.update({ { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 } });
    CHECK(f.dialog->hover().text == u"Start one");
}
