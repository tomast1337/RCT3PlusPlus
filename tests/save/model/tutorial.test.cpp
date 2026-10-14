// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/tutorial.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::save;

namespace
{
    // Field names follow the format; every value is invented.
    Value step(std::string title, std::string text, std::vector<std::string> triggers, std::int64_t position, std::int64_t width)
    {
        std::vector<Value> trigger_values;
        for (auto& t : triggers)
        {
            std::vector<Value> fields;
            fields.push_back(Value::make_string(std::move(t)));
            fields.push_back(Value::make_integer(3));
            trigger_values.push_back(Value::make_struct({ "Name", "Type" }, std::move(fields)));
        }
        std::vector<Value> fields;
        fields.push_back(Value::make_string(std::move(title)));
        fields.push_back(Value::make_string(std::move(text)));
        fields.push_back(Value::make_list(std::move(trigger_values)));
        fields.push_back(Value::make_integer(position));
        fields.push_back(Value::make_integer(width));
        return Value::make_struct({ "StepTitle", "Text", "Triggers", "WindowPos", "WindowWidth" }, std::move(fields));
    }
} // namespace

TEST_CASE("a tutorial object gives its name, title and steps in order")
{
    std::vector<Value> steps;
    steps.push_back(step("First", "Look around.", { }, 0, 300));
    steps.push_back(step("Second", "Build a stall.", { "StallPlaced", "WindowOpened" }, 1, 250));
    std::vector<Value> fields;
    fields.push_back(Value::make_integer(1));
    fields.push_back(Value::make_list(std::move(steps)));
    fields.push_back(Value::make_string("Lessons"));
    fields.push_back(Value::make_string("Lessons in a park"));
    DecodedObject object;
    object.fields = Value::make_struct({ "Flags", "Steps", "TutorialName", "TutorialTitle" }, std::move(fields));

    const TutorialScript t = tutorial_from(object);
    CHECK(t.name == "Lessons");
    CHECK(t.title == "Lessons in a park");
    REQUIRE(t.steps.size() == 2);
    CHECK(t.steps[0].title == "First");
    CHECK(t.steps[0].text == "Look around.");
    CHECK(t.steps[0].triggers.empty());
    CHECK(t.steps[0].window_width == 300);
    CHECK(t.steps[1].triggers == std::vector<std::string> { "StallPlaced", "WindowOpened" });
    CHECK(t.steps[1].window_position == 1);
}

TEST_CASE("missing or mistyped fields leave the tutorial empty rather than failing")
{
    std::vector<Value> fields;
    fields.push_back(Value::make_integer(7));
    DecodedObject object;
    object.fields = Value::make_struct({ "Steps" }, std::move(fields));
    const TutorialScript t = tutorial_from(object);
    CHECK(t.name.empty());
    CHECK(t.steps.empty());
}

TEST_CASE("a save without a tutorial has none")
{
    const SaveFile empty;
    const auto t = load_tutorial(empty);
    REQUIRE(t);
    CHECK_FALSE(t->has_value());
}
