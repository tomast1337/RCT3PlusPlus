// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/message_drawer.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/park_bar.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        StringTable strings;
        Desktop desktop;
        MessageDrawer* drawer = nullptr;
        ParkBar* bar = nullptr;
        std::vector<DrawerMessage> messages;

        Fixture()
        {
            drawer = &desktop.root().add<MessageDrawer>("messages", game_skins::message_drawer_style(), strings);
            bar = &desktop.root().add<ParkBar>("bar", game_skins::park_bar_style(), strings);
            desktop.layout({ 0, 0, 1000, 700 });
            desktop.update({ });
        }

        /// Runs the desktop for `seconds` in steps, the pointer away from everything.
        void run(float seconds, float step = 0.1f)
        {
            for (float t = 0; t < seconds; t += step)
            {
                desktop.layout({ 0, 0, 1000, 700 });
                desktop.update({ { 900, 650 }, false }, { }, { step, nullptr });
            }
        }

        void post(std::size_t count)
        {
            for (std::size_t i = 0; i < count; ++i)
                messages.push_back({ messages.size() + 1, u"news" });
            drawer->show(messages);
        }

        void press(Point p)
        {
            desktop.layout({ 0, 0, 1000, 700 });
            desktop.update({ p, true }, { }, { 0.1f, nullptr });
            desktop.update({ p, false }, { }, { 0.1f, nullptr });
        }
    };
} // namespace

TEST_CASE("a message's text fills in its subject's name, or leaves the name out")
{
    StringTable strings;
    strings.add("Opened", u"New attraction: %NAME%");
    strings.add("Warned", u"Soon the %NAME% test is due");
    strings.add("Plain", u"100% fun");
    simulation::ParkMessage m;
    m.text_key = "Opened";
    m.subject_name = "Big Wheel";
    CHECK(message_text(m, strings) == u"New attraction: Big Wheel");
    m.subject_name.clear();
    CHECK(message_text(m, strings) == u"New attraction");
    m.text_key = "Warned";
    CHECK(message_text(m, strings) == u"Soon the test is due");
    m.text_key = "Plain";
    CHECK(message_text(m, strings) == u"100% fun");
}

TEST_CASE("the drawer hides behind the bar, reached through the notch in its middle")
{
    Fixture f;
    const Rect d = f.drawer->rect();
    const Rect b = f.bar->rect();
    CHECK(d.width() == message_drawer::kWidth);
    CHECK(d.y0 == message_drawer::kTop);
    CHECK(d.y1 <= b.y1);
    CHECK(d.x0 > b.x0);
    CHECK(d.x1 < b.x1);
    const float notch = b.x0 + park_bar::kLeftWidth + 19;
    CHECK(rct3::as_pointer(f.desktop.hit({ notch, b.y1 - 4 })) == f.drawer);
    CHECK(rct3::as_pointer(f.desktop.hit({ notch - 40, b.y1 - 4 })) == f.bar);
    CHECK(rct3::as_pointer(f.desktop.hit({ notch, b.y0 + 1 })) == f.bar);
}

TEST_CASE("messages the drawer starts with are not new; later ones slide it down, then back up")
{
    Fixture f;
    f.post(2);
    CHECK_FALSE(f.drawer->take_alert());
    f.run(1);
    CHECK(f.drawer->height() == message_drawer::kClosed);

    f.post(3);
    CHECK(f.drawer->take_alert());
    CHECK_FALSE(f.drawer->take_alert());
    f.run(2);
    const float shown = f.drawer->height();
    CHECK(shown > message_drawer::kClosed);
    CHECK(shown < message_drawer::kOpen);
    f.run(message_drawer::kHoldSeconds - 2);
    CHECK(f.drawer->height() == shown);
    f.run(4);
    CHECK(f.drawer->height() == message_drawer::kClosed);
}

TEST_CASE("dragging sets where the drawer rests; a double press opens or closes it")
{
    Fixture f;
    f.post(1);
    const Rect b = f.bar->rect();
    const Point notch { b.x0 + park_bar::kLeftWidth + 19, b.y1 - 4 };
    f.desktop.update({ notch, true }, { }, { 1, nullptr });
    f.desktop.update({ { notch.x, notch.y + 100 }, true }, { }, { 0.1f, nullptr });
    f.desktop.update({ { notch.x, notch.y + 100 }, false }, { }, { 0.1f, nullptr });
    const float dragged = f.drawer->height();
    CHECK(dragged == doctest::Approx(message_drawer::kClosed + 100).epsilon(0.05));
    f.post(1);
    f.run(15);
    CHECK(f.drawer->height() == doctest::Approx(dragged));

    f.run(1);
    const Point lip { f.drawer->rect().x0 + 100, f.drawer->rect().y1 - 10 };
    f.press(lip);
    f.press(lip);
    CHECK(f.drawer->height() == message_drawer::kClosed);
    f.run(1);
    f.press(notch);
    f.press(notch);
    CHECK(f.drawer->height() == message_drawer::kOpen);
}

TEST_CASE("the arrows step through the messages when they do not all fit")
{
    Fixture f;
    f.post(5);
    f.run(0.2f);
    CHECK_FALSE(f.drawer->older_button().enabled);
    CHECK_FALSE(f.drawer->newer_button().enabled);

    f.post(30);
    f.run(0.2f);
    CHECK(f.drawer->older_button().enabled);
    CHECK_FALSE(f.drawer->newer_button().enabled);
    f.drawer->older_button().click();
    f.run(0.2f);
    CHECK(f.drawer->skipped() == 1);
    CHECK(f.drawer->newer_button().enabled);
    f.drawer->newer_button().click();
    f.run(0.2f);
    CHECK(f.drawer->skipped() == 0);
}
