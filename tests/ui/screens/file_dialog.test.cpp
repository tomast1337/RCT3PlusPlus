// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/file_dialog.hpp"

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
        FileDialog* dialog = nullptr;

        explicit Fixture(FileDialogMode mode = FileDialogMode::Load)
        {
            strings.add("LGD_Title", u"Open a park");
            strings.add("SGD_Title", u"Keep a park");
            strings.add("FD_Scanning", u"Reading %d / %d");
            strings.add("FD_ScanComplete", u"All read");
            strings.add("FD_NoFiles", u"Empty");
            strings.add("FD_DeleteFile", u"Remove %FILENAME%?");
            strings.add("FD_RenameFile", u"%OLDNAME% becomes %NEWNAME%?");
            strings.add("FD_RenameFailed", u"Could not rename");
            strings.add("SGD_Save", u"Replace %FILENAME%?");
            strings.add("SGD_NeedAName", u"Name it");
            strings.add("ShortMonth1", u"Sec");
            strings.add("Year", u"Season");
            strings.add("ManyGuests", u"# visitors");
            dialog = &desktop.root().add<FileDialog>("files", game_skins::file_dialog_style(kGamePalette), strings, mode, true);
            desktop.layout({ 0, 0, 1024, 768 });
            desktop.update({ });
        }

        void tick(float seconds = 0)
        {
            desktop.update({ }, { }, { seconds, nullptr });
        }

        /// Answers the question showing with its first (yes) or last (no) answer.
        void answer(bool yes)
        {
            auto q = dialog->question();
            REQUIRE(q);
            (yes ? q->buttons().front() : q->buttons().back())->click();
            tick();
            tick();
        }
    };

    save::SaveSummary park()
    {
        save::SaveSummary s;
        s.park_name = "Hilltop";
        s.day = 2;
        s.month = 1;
        s.year = 3;
        s.minutes = 600;
        s.guests = 40;
        return s;
    }
} // namespace

TEST_CASE("the window is titled by what it does, and only saving has a name box")
{
    Fixture load;
    CHECK(load.dialog->title == u"Open a park");
    CHECK(!load.dialog->name_box());
    Fixture save(FileDialogMode::Save);
    CHECK(save.dialog->title == u"Keep a park");
    CHECK(save.dialog->name_box());
}

TEST_CASE("rows fill in as the files are read, the line under the list counting them")
{
    Fixture f;
    f.dialog->set_files({ u"First.dat", u"Second.DAT" });
    CHECK(f.dialog->row_name_box(0).text == u"First");
    CHECK(f.dialog->row_name_box(1).text == u"Second");
    CHECK(f.dialog->row_lines(0)[0] == u"<Scanning...>");
    CHECK(f.dialog->status().text == u"Reading 1 / 2");

    f.dialog->set_summary(0, park(), 7);
    CHECK(f.dialog->row_lines(0)[0] == u"Hilltop");
    CHECK(f.dialog->row_lines(0)[1] == u"2 Sec, Season 3  10:00 am");
    CHECK(f.dialog->row_lines(0)[2] == u"40 visitors");
    CHECK(f.dialog->status().text == u"Reading 2 / 2");

    f.dialog->set_summary(1, std::nullopt, 0); // Not a save: its lines stay empty
    CHECK(f.dialog->row_lines(1)[0].empty());
    CHECK(f.dialog->status().text == u"All read");
    f.tick(2.5f);
    CHECK(f.dialog->status().text == u"All read");
    CHECK(f.dialog->status().style.colour.r > 0); // Fading out
    f.tick(1);
    CHECK(f.dialog->status().text.empty());

    f.dialog->set_files({ });
    CHECK(f.dialog->status().text == u"Empty");
}

TEST_CASE("a row clicked twice soon opens its file and closes the window")
{
    Fixture f;
    f.dialog->set_files({ u"First.dat", u"Second.dat" });
    f.dialog->click_row(1);
    f.tick();
    CHECK(f.dialog->selected() == 1);
    CHECK_FALSE(f.dialog->take_open());
    f.tick(1);
    f.dialog->click_row(1); // Too late for a double click: only picks it again
    f.tick();
    CHECK_FALSE(f.dialog->take_open());
    f.dialog->click_row(1);
    f.tick(0.1f);
    CHECK(f.dialog->take_open() == u"Second.dat");
    CHECK(f.dialog->closing());
}

TEST_CASE("the open button opens the picked file, and nothing without one")
{
    Fixture f;
    f.dialog->set_files({ u"First.dat" });
    f.dialog->open_button().click();
    f.tick();
    CHECK_FALSE(f.dialog->take_open());
    f.dialog->select(0);
    f.dialog->open_button().click();
    f.tick();
    CHECK(f.dialog->take_open() == u"First.dat");
}

TEST_CASE("deleting asks first")
{
    Fixture f;
    f.dialog->set_files({ u"First.dat", u"Second.dat" });
    f.dialog->select(1);
    f.dialog->delete_button().click();
    f.tick();
    REQUIRE(f.dialog->question());
    CHECK(f.dialog->question()->text().text == u"Remove Second?");
    f.answer(false);
    CHECK_FALSE(f.dialog->take_delete());
    f.dialog->delete_button().click();
    f.tick();
    f.answer(true);
    CHECK(f.dialog->take_delete() == u"Second.dat");
    CHECK_FALSE(f.dialog->closing());
}

TEST_CASE("renaming in a row asks first, and a failed rename puts the name back")
{
    Fixture f;
    f.dialog->set_files({ u"First.dat" });
    EditBox& box = f.dialog->row_name_box(0);
    box.text = u"Renamed";
    box.on_key({ Key::Enter });
    f.tick();
    REQUIRE(f.dialog->question());
    CHECK(f.dialog->question()->text().text == u"First becomes Renamed?");
    f.answer(false);
    CHECK(box.text == u"First");
    CHECK_FALSE(f.dialog->take_rename());

    box.text = u"Renamed";
    box.on_key({ Key::Enter });
    f.tick();
    f.answer(true);
    const auto rename = f.dialog->take_rename();
    REQUIRE(rename);
    CHECK(rename->first == u"First.dat");
    CHECK(rename->second == u"Renamed.dat");
    f.dialog->rename_failed();
    CHECK(box.text == u"First");
    REQUIRE(f.dialog->question());
    CHECK(f.dialog->question()->text().text == u"Could not rename");
}

TEST_CASE("saving checks the name, adds the extension and asks before replacing")
{
    Fixture f(FileDialogMode::Save);
    f.dialog->set_files({ u"Old.park" });
    EditBox& name = *f.dialog->name_box();

    f.dialog->open_button().click();
    f.tick();
    REQUIRE(f.dialog->question());
    CHECK(f.dialog->question()->text().text == u"Name it");
    f.answer(true);
    CHECK_FALSE(f.dialog->take_open());

    f.dialog->select(0); // Picking a row names the save after it
    CHECK(name.text == u"Old");
    name.text = u"old";
    f.dialog->open_button().click();
    f.tick();
    REQUIRE(f.dialog->question());
    CHECK(f.dialog->question()->text().text == u"Replace Old?");
    f.answer(false);
    CHECK_FALSE(f.dialog->take_open());

    name.text = u"New Park";
    f.dialog->open_button().click();
    f.tick();
    CHECK(f.dialog->take_open() == u"New Park.park");
    CHECK(f.dialog->closing());
}

TEST_CASE("file names show without their extension, and get it when saved")
{
    CHECK(shown_file_name(u"Park.DaT") == u"Park");
    CHECK(shown_file_name(u"Park.sav") == u"Park.sav");
    CHECK(shown_file_name(u"Park.park") == u"Park");
    CHECK(with_save_extension(u"Park") == u"Park.park");
    CHECK(with_save_extension(u"Park.DAT") == u"Park.park");
    CHECK(with_save_extension(u"Park.park") == u"Park.park");
}

TEST_CASE("kept files open but are not deleted, renamed or saved over")
{
    Fixture f(FileDialogMode::Save);
    f.dialog->set_files({ u"Mine.dat", u"Theirs.dat" }, { false, true });
    CHECK_FALSE(f.dialog->kept(0));
    CHECK(f.dialog->kept(1));
    CHECK(f.dialog->row_name_box(0).enabled);
    CHECK_FALSE(f.dialog->row_name_box(1).enabled);

    f.dialog->select(1);
    f.tick();
    CHECK_FALSE(f.dialog->delete_button().enabled);
    f.dialog->delete_button().click();
    f.tick();
    CHECK(!f.dialog->question());
    CHECK_FALSE(f.dialog->take_delete());

    // A save of the same name goes beside the kept file, so nothing asks to replace it.
    f.dialog->open_button().click();
    f.tick();
    CHECK(!f.dialog->question());
    CHECK(f.dialog->take_open() == u"Theirs.park");
}

TEST_CASE("with saving switched off the save button is greyed and nothing is saved")
{
    Fixture f(FileDialogMode::Save);
    f.dialog->set_files({ });
    f.dialog->set_file_name(u"Hilltop.dat");
    CHECK(f.dialog->name_box()->text == u"Hilltop");
    f.dialog->set_can_save(false);
    CHECK_FALSE(f.dialog->can_save());
    CHECK_FALSE(f.dialog->open_button().enabled);
    f.dialog->open_button().click();
    f.tick();
    CHECK(!f.dialog->question());
    CHECK_FALSE(f.dialog->take_open());

    Fixture load;
    load.dialog->set_can_save(false); // Loading is not saving
    CHECK(load.dialog->open_button().enabled);
}
