// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/label.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

TEST_CASE("labels draw in their role's font and skip roles without one")
{
    ovl::Font f;
    f.metrics = { 0.1f, 1.0f, 0.2f };
    f.has_code_table = true;
    f.glyphs.push_back({ u'x', 1, 0, { 0, 0, 0.1f, 0.1f }, { 0, 0 }, 0 });
    const std::array<Font::Page, 1> pages = { { { 9, 100, 100 } } };
    FontSet fonts;
    fonts.set(FontRole::Medium, Font::from(f, pages));

    Desktop desktop;
    Label& medium = desktop.root().add<Label>("m", u"xx", FontRole::Medium);
    medium.placement = top_left(0, 0, 100, 20);
    desktop.root().add<Label>("l", u"xx", FontRole::Large);
    desktop.layout({ 0, 0, 200, 100 });
    CHECK_FALSE(medium.takes_pointer());

    DrawList list;
    const SkinLibrary skins;
    DrawContext context { list, skins, fonts };
    desktop.draw(context);
    CHECK(list.vertices().size() == 8);
    REQUIRE(list.batches().size() == 1);
    CHECK(list.batches()[0].texture == 9);
}

TEST_CASE("a copyable label is selected by a press and copied whole")
{
    Desktop desktop;
    Label& name = desktop.root().add<Label>("name", u"Big Wheel \U0001F3A1");
    name.placement = top_left(0, 0, 100, 20);
    name.copyable = true;
    desktop.layout({ 0, 0, 200, 100 });

    desktop.update({ { 10, 10 }, true, true });
    desktop.update({ { 10, 10 }, false, true });
    CHECK(rct3::as_pointer(desktop.keyboard_focus()) == &name);
    KeyboardState keys;
    keys.presses.push_back({ Key::Copy, false, true });
    desktop.update({ { 10, 10 }, false, true }, keys);
    CHECK(desktop.take_clipboard() == u"Big Wheel \U0001F3A1");
}
