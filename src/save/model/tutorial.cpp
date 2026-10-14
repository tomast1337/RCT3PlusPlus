// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/tutorial.hpp"

namespace rct3::save
{

    namespace
    {
        namespace names
        {
            constexpr std::string_view kClass = "Tutorial";
            constexpr std::string_view kName = "TutorialName";
            constexpr std::string_view kTitle = "TutorialTitle";
            constexpr std::string_view kSteps = "Steps";
            constexpr std::string_view kStepTitle = "StepTitle";
            constexpr std::string_view kText = "Text";
            constexpr std::string_view kTriggers = "Triggers";
            constexpr std::string_view kTriggerName = "Name";
            constexpr std::string_view kWindowPos = "WindowPos";
            constexpr std::string_view kWindowWidth = "WindowWidth";
        } // namespace names

        std::string text_of(const Value* v)
        {
            return v != nullptr && v->kind() == ValueKind::String ? std::string(v->text()) : std::string { };
        }

        std::int32_t int_of(const Value* v)
        {
            return v != nullptr && v->kind() == ValueKind::Integer ? static_cast<std::int32_t>(v->as_integer()) : 0;
        }

        std::span<const Value> list_of(const Value* v)
        {
            return v != nullptr && v->kind() == ValueKind::List ? v->items() : std::span<const Value> { };
        }

        TutorialStep step_from(const Value& v)
        {
            TutorialStep s;
            if (v.kind() != ValueKind::Struct)
                return s;
            s.title = text_of(v.field(names::kStepTitle));
            s.text = text_of(v.field(names::kText));
            s.window_position = int_of(v.field(names::kWindowPos));
            s.window_width = int_of(v.field(names::kWindowWidth));
            for (const Value& t : list_of(v.field(names::kTriggers)))
                s.triggers.push_back(t.kind() == ValueKind::Struct ? text_of(t.field(names::kTriggerName)) : std::string { });
            return s;
        }
    } // namespace

    TutorialScript tutorial_from(const DecodedObject& object)
    {
        TutorialScript t;
        t.name = text_of(object.find(names::kName));
        t.title = text_of(object.find(names::kTitle));
        for (const Value& v : list_of(object.find(names::kSteps)))
            t.steps.push_back(step_from(v));
        return t;
    }

    std::expected<std::optional<TutorialScript>, Error> load_tutorial(const SaveFile& file)
    {
        for (const Object& o : file.objects)
        {
            if (o.class_index >= file.classes.size() || file.classes[o.class_index].name != names::kClass)
                continue;
            const auto decoded = decode_object(file, o);
            if (!decoded)
                return std::unexpected(decoded.error());
            return tutorial_from(*decoded);
        }
        return std::nullopt;
    }

} // namespace rct3::save
