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
