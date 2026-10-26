// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/message_drawer.hpp"

#include "simulation/awards.hpp"
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
            drawer = &desktop.root().add<MessageDrawer>("messages", game_skins::message_drawer_style(kGamePalette), strings);
            bar = &desktop.root().add<ParkBar>("bar", game_skins::park_bar_style(kGamePalette), strings);
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

TEST_CASE("a message's other field is filled from its field key")
{
    StringTable strings;
    strings.add("Broken", u"%NAME% broke down (%KIND%)");
    strings.add("Jammed", u"doors jammed");
    simulation::ParkMessage m { .text_key = "Broken", .subject_name = "Big Wheel", .field_key = "Jammed" };
    CHECK(message_text(m, strings) == u"Big Wheel broke down (doors jammed)");
}

TEST_CASE("a message's number fills its %d")
{
    StringTable strings;
    strings.add("Crashed", u"A crash on %NAME% has injured %d people!");
    simulation::ParkMessage m { .text_key = "Crashed", .subject_name = "Big Dipper", .number = 16 };
    CHECK(message_text(m, strings) == u"A crash on Big Dipper has injured 16 people!");
}

TEST_CASE("an award's message is named by the award, a fine's too")
{
    StringTable strings;
    strings.add("Award_Award", u"Won: %NAME%");
    strings.add("Award_Fine", u"Fined: %NAME%");
    strings.add("Award_Best_Toilets", u"Best Toilets");
    strings.add("Award_Most_Untidy", u"Most Untidy");
    const simulation::ParkMessage won { .text_key = std::string(simulation::award_message(11)), .award = 11 };
    CHECK(message_text(won, strings) == u"Won: Best Toilets");
    const simulation::ParkMessage fined { .text_key = std::string(simulation::award_message(1)), .award = 1 };
    CHECK(message_text(fined, strings) == u"Fined: Most Untidy");
    CHECK(simulation::award_name(simulation::kAwardKinds).empty());
}

TEST_CASE("every message has an icon for its kind; only an award's leads somewhere by itself")
{
    StringTable strings;
    const MessageDrawerStyle style = game_skins::message_drawer_style(kGamePalette);
    simulation::ParkMessages messages;
    simulation::post(messages, { .text_key = "Award_Award", .award = 11 });
    simulation::post(messages, { .text_key = "ObjectiveMessage_WeekWarning" });
    simulation::post(messages, { .text_key = "Thought_PI_Park_Litter1" });
    simulation::post(messages, { .text_key = "RideFixedMessage", .subject = 7 });
    const std::vector<DrawerMessage> shown = drawer_messages(messages, strings, style);
    REQUIRE(shown.size() == 4);
    CHECK(shown[0].icon == style.award);
    CHECK(shown[0].clickable);
    CHECK(shown[1].icon == style.objective);
    CHECK(shown[2].icon == style.inspector);
    CHECK(shown[3].icon == style.locate);
    for (std::size_t i = 1; i < shown.size(); ++i)
        CHECK_FALSE(shown[i].clickable);
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

TEST_CASE("a clickable icon is heard when pressed; an icon that leads nowhere drags the drawer")
{
    Fixture f;
    f.messages = { { .serial = 1, .text = u"about nothing", .icon = "camera" }, { .serial = 2, .text = u"about a ride", .icon = "ride", .clickable = true } };
    f.drawer->show(f.messages);
    f.run(1);
    const Rect b = f.bar->rect();
    const Point notch { b.x0 + park_bar::kLeftWidth + 19, b.y1 - 4 };
    f.press(notch);
    f.press(notch);
    REQUIRE(f.drawer->height() == message_drawer::kOpen);
    f.run(1);

    // Rows without a font are the empty row's height, the newest at the bottom.
    const Rect r = f.drawer->rect();
    f.press({ r.x0 + 27, r.y1 - 53 });
    CHECK_FALSE(f.drawer->take_clicked());
    f.run(1);
    f.press({ r.x0 + 27, r.y1 - 35 });
    CHECK(f.drawer->take_clicked() == 2u);
    CHECK_FALSE(f.drawer->take_clicked());
    CHECK(f.drawer->height() == message_drawer::kOpen);
}
