// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/display.hpp"

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <algorithm>
#include <bit>
#include <string_view>

namespace rct3::particles
{

    namespace
    {

        constexpr std::string_view kDisplayClass = "SavedFireworkDisplay";

        /// A whole number field. Some files declare one as a 32-bit float yet store the number's own bits.
        std::int64_t whole(const save::Value& parent, std::string_view name)
        {
            const auto v = parent.field(name);
            if (!v)
                return 0;
            if (v->kind() == save::ValueKind::Integer)
                return v->as_integer();
            if (v->kind() == save::ValueKind::Float)
                return std::bit_cast<std::int32_t>(v->as_float());
            return 0;
        }

        float number(const save::Value& parent, std::string_view name)
        {
            const auto v = parent.field(name);
            if (v && v->kind() == save::ValueKind::Float)
                return v->as_float();
            return v && v->kind() == save::ValueKind::Integer ? static_cast<float>(v->as_integer()) : 0.0f;
        }

        std::string text(const save::Value& parent, std::string_view name)
        {
            const auto v = parent.field(name);
            return v && v->kind() == save::ValueKind::String ? std::string(v->text()) : std::string { };
        }

        std::span<const save::Value> list(const save::Value& parent, std::string_view name)
        {
            const auto v = parent.field(name);
            return v && v->kind() == save::ValueKind::List ? v->items() : std::span<const save::Value> { };
        }

    } // namespace

    const char* to_string(DisplayError e) noexcept
    {
        switch (e)
        {
            case DisplayError::Container:
                return "not a readable container";
            case DisplayError::NoDisplay:
                return "no display in it";
            case DisplayError::BadObject:
                return "an object could not be decoded";
            case DisplayError::BadLauncher:
                return "a shot names a launcher the display does not have";
        }
        return "unknown";
    }

    std::expected<Display, DisplayError> read_display(std::span<const std::byte> data)
    {
        const auto file = save::read_save(data, { .verify_checksum = false });
        if (!file)
            return std::unexpected(DisplayError::Container);
        const auto root = std::ranges::find_if(file->objects, [&](const save::Object& o) { return o.class_index < file->classes.size() && file->classes[o.class_index].name == kDisplayClass; });
        if (root == file->objects.end())
            return std::unexpected(DisplayError::NoDisplay);
        const auto o = save::decode_object(*file, *root);
        if (!o)
            return std::unexpected(DisplayError::BadObject);
        const save::Value& fields = o->fields;
        const save::ObjectIndex index(*file);

        Display display {
            .name = text(fields, "Name"),
            .music = text(fields, "MusicSelection"),
            .months = static_cast<std::uint32_t>(whole(fields, "MonthMask")),
            .start_minutes = static_cast<std::int32_t>(whole(fields, "StartTimeMinutes")),
        };
        for (const save::Value& entry : list(fields, "SceneryEntries"))
        {
            Launcher l {
                .col = static_cast<std::int32_t>(whole(entry, "Col")),
                .row = static_cast<std::int32_t>(whole(entry, "Row")),
                .corner = static_cast<std::int32_t>(whole(entry, "Corner")),
                .direction = static_cast<std::int32_t>(whole(entry, "Dir")),
            };
            // The item is a reference to a small object naming it.
            if (const auto sid = entry.field("SID"); sid && sid->kind() == save::ValueKind::Reference)
                if (const auto item = index.find(sid->as_reference()))
                {
                    const auto named = save::decode_object(*file, *item);
                    if (!named)
                        return std::unexpected(DisplayError::BadObject);
                    l.item = text(named->fields, "SYMBOLNAME");
                }
            display.launchers.push_back(std::move(l));
        }
        for (const save::Value& entry : list(fields, "Entries"))
        {
            std::string effect = text(entry, "Name");
            // A path inside the install, such as "Fireworks\Name.frw": the name alone.
            effect.erase(0, effect.find_last_of("\\/") + 1);
            if (const auto dot = effect.rfind('.'); dot != std::string::npos)
                effect.resize(dot);
            const std::int64_t launcher = whole(entry, "SceneryEntry");
            if (launcher < 0 || static_cast<std::size_t>(launcher) >= display.launchers.size())
                return std::unexpected(DisplayError::BadLauncher);
            display.shots.push_back(
                {
                    .effect = std::move(effect),
                    .launcher = static_cast<std::size_t>(launcher),
                    .tube = static_cast<std::int32_t>(whole(entry, "SlotIndex")),
                    .seconds = number(entry, "Time"),
                    .lane = static_cast<std::int32_t>(whole(entry, "Track")),
                });
        }
        std::ranges::stable_sort(display.shots, { }, &Shot::seconds);
        return display;
    }

} // namespace rct3::particles
