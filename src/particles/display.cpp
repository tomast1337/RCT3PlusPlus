// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/display.hpp"

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <string_view>

namespace rct3::particles
{

    namespace
    {

        constexpr std::string_view kDisplayClass = "SavedFireworkDisplay";
        constexpr std::string_view kParkDisplayClass = "FireworkDisplay";

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

    namespace
    {

        /// The display `fields` hold, but for its launchers and its shots.
        Display display_of(const save::Value& fields)
        {
            return {
                .name = text(fields, "Name"),
                .music_file = text(fields, "MusicSelection"),
                .months = static_cast<std::uint32_t>(whole(fields, "MonthMask")),
                .start_minutes = static_cast<std::int32_t>(whole(fields, "StartTimeMinutes")),
            };
        }

        /// A shot entry's effect, tube, time and lane, fired from `launcher`.
        Shot shot_of(const save::Value& entry, std::size_t launcher)
        {
            std::string effect = text(entry, "Name");
            // A path inside the install, such as "Fireworks\Name.frw": the name alone.
            effect.erase(0, effect.find_last_of("\\/") + 1);
            if (const auto dot = effect.rfind('.'); dot != std::string::npos)
                effect.resize(dot);
            const std::int64_t kind = whole(entry, "EffectType");
            return {
                .kind = kind == 1 ? ShotKind::Laser
                    : kind == 2   ? ShotKind::WaterJet
                                  : ShotKind::Firework,
                .effect = std::move(effect),
                .launcher = launcher,
                .tube = static_cast<std::int32_t>(whole(entry, "SlotIndex")),
                .seconds = number(entry, "Time"),
                .lane = static_cast<std::int32_t>(whole(entry, "Track")),
            };
        }

        bool of_class(const save::SaveFile& file, const save::Object& o, std::string_view name)
        {
            return o.class_index < file.classes.size() && file.classes[o.class_index].name == name;
        }

        /// The fields of the object `parent`'s reference `name` points to; none when it points nowhere.
        std::optional<save::Value> referred(const save::SaveFile& file, const save::ObjectIndex& index, const save::Value& parent, std::string_view name)
        {
            const auto ref = parent.field(name);
            if (!ref || ref->kind() != save::ValueKind::Reference)
                return std::nullopt;
            const auto o = index.find(ref->as_reference());
            if (!o)
                return std::nullopt;
            auto decoded = save::decode_object(file, *o);
            if (!decoded)
                return std::nullopt;
            return std::move(decoded->fields);
        }

        /// A blender of numbers: its first points, each a time up to its greatest and a number, as a curve over 0 to 1.
        Curve blended(const save::SaveFile& file, const save::ObjectIndex& index, const save::Value& blender)
        {
            const float longest = number(blender, "MaxTime");
            const auto points = list(blender, "FixedPts");
            const std::size_t used = static_cast<std::size_t>(std::clamp<std::int64_t>(whole(blender, "NumPts"), 0, static_cast<std::int64_t>(points.size())));
            Curve c;
            for (const save::Value& point : points.first(used))
                if (const auto element = referred(file, index, point, "BlenderElement"))
                {
                    c.times.push_back(longest > 0 ? number(point, "PointTime") / longest : 0.0f);
                    c.values.push_back(number(*element, "Float"));
                }
            return c;
        }

        /// The pattern of a park's water jet shot: from the jet source the shot made, its description.
        std::optional<JetPattern> jet_pattern(const save::SaveFile& file, const save::ObjectIndex& index, const save::Value& entry)
        {
            const auto source = referred(file, index, entry, "ParticleSourceRef");
            const auto desc = source ? referred(file, index, *source, "WaterJetBaseDesc") : std::nullopt;
            if (!desc)
                return std::nullopt;
            const auto curve = [&](std::string_view name) {
                const auto blender = referred(file, index, *desc, name);
                return blender ? blended(file, index, *blender) : Curve { };
            };
            return JetPattern { .seconds = number(*desc, "Duration"), .pressure = curve("PressureBlender"), .elevation = curve("ElevationBlender"), .widening = curve("DiameterDeltaBlender") };
        }

    } // namespace

    std::expected<Display, DisplayError> read_display(std::span<const std::byte> data)
    {
        const auto file = save::read_save(data, { .verify_checksum = false });
        if (!file)
            return std::unexpected(DisplayError::Container);
        const auto root = std::ranges::find_if(file->objects, [&](const save::Object& o) { return of_class(*file, o, kDisplayClass); });
        if (root == file->objects.end())
            return std::unexpected(DisplayError::NoDisplay);
        const auto o = save::decode_object(*file, *root);
        if (!o)
            return std::unexpected(DisplayError::BadObject);
        const save::Value& fields = o->fields;
        const save::ObjectIndex index(*file);

        Display display = display_of(fields);
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
            const std::int64_t launcher = whole(entry, "SceneryEntry");
            if (launcher < 0 || static_cast<std::size_t>(launcher) >= display.launchers.size())
                return std::unexpected(DisplayError::BadLauncher);
            display.shots.push_back(shot_of(entry, static_cast<std::size_t>(launcher)));
        }
        std::ranges::stable_sort(display.shots, { }, &Shot::seconds);
        return display;
    }

    std::expected<std::vector<Display>, DisplayError> read_park_displays(const save::SaveFile& file)
    {
        std::vector<Display> displays;
        const save::ObjectIndex index(file);
        for (const save::Object& root : file.objects)
        {
            if (!of_class(file, root, kParkDisplayClass))
                continue;
            const auto o = save::decode_object(file, root);
            if (!o)
                return std::unexpected(DisplayError::BadObject);
            // A show ride keeps its show's timeline as a display too; the ride puts it on, not the calendar. A park's
            // own display may name a show controller too, one no longer in the park.
            if (const auto show = o->fields.field("ShowController"); show && show->kind() == save::ValueKind::Reference)
                if (const auto controller = index.find(show->as_reference()); controller && of_class(file, *controller, "ShowController"))
                    continue;
            Display display = display_of(o->fields);
            for (const save::Value& entry : list(o->fields, "Entries"))
            {
                const auto item = entry.field("SceneryItemRef");
                if (!item || item->kind() != save::ValueKind::Reference)
                    continue;
                const std::uint64_t id = item->as_reference();
                auto launcher = std::ranges::find(display.launchers, std::optional(id), &Launcher::placed);
                if (launcher == display.launchers.end())
                    launcher = display.launchers.insert(launcher, Launcher { .placed = id });
                Shot& shot = display.shots.emplace_back(shot_of(entry, static_cast<std::size_t>(launcher - display.launchers.begin())));
                if (shot.kind == ShotKind::WaterJet)
                    shot.jet = jet_pattern(file, index, entry);
            }
            std::ranges::stable_sort(display.shots, { }, &Shot::seconds);
            displays.push_back(std::move(display));
        }
        return displays;
    }

    ShotKind main_kind(const Display& display)
    {
        std::array<std::size_t, 3> counts { };
        for (const Shot& shot : display.shots)
            ++counts[static_cast<std::size_t>(shot.kind)];
        return static_cast<ShotKind>(std::ranges::max_element(counts) - counts.begin());
    }

    float display_seconds(const Display& display)
    {
        float last = 0;
        for (const Shot& shot : display.shots)
            last = std::max(last, shot.seconds + (shot.jet ? shot.jet->seconds : 0.0f));
        return last;
    }

    bool display_due(const Display& display, std::int32_t month, float minutes)
    {
        const bool in_month = month >= 0 && month < 32 && (display.months >> month & 1u) != 0;
        const float since = minutes - static_cast<float>(display.start_minutes);
        return in_month && since >= 0 && since < kStartWindowMinutes;
    }

    std::span<const Shot> advance_display(DisplayRun& run, const Display& display, float seconds)
    {
        run.seconds += seconds;
        const std::size_t first = std::min(run.next_shot, display.shots.size());
        std::size_t last = first;
        while (last < display.shots.size() && display.shots[last].seconds <= run.seconds)
            ++last;
        run.next_shot = last;
        return std::span(display.shots).subspan(first, last - first);
    }

} // namespace rct3::particles
