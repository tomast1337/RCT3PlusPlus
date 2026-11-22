// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/display.hpp"

#include "core/types.hpp"
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
        i64 whole(const save::Value& parent, std::string_view name)
        {
            const auto v = parent.field(name);
            if (!v)
                return 0;
            if (v->kind() == save::ValueKind::Integer)
                return v->as_integer();
            if (v->kind() == save::ValueKind::Float)
                return std::bit_cast<i32>(v->as_float());
            return 0;
        }

        f32 number(const save::Value& parent, std::string_view name)
        {
            const auto v = parent.field(name);
            if (v && v->kind() == save::ValueKind::Float)
                return v->as_float();
            return v && v->kind() == save::ValueKind::Integer ? static_cast<f32>(v->as_integer()) : 0.0f;
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
                .months = static_cast<u32>(whole(fields, "MonthMask")),
                .start_minutes = static_cast<i32>(whole(fields, "StartTimeMinutes")),
            };
        }

        /// A shot entry's effect, tube, time and lane, fired from `launcher`.
        Shot shot_of(const save::Value& entry, usize launcher)
        {
            std::string effect = text(entry, "Name");
            // A path inside the install, such as "Fireworks\Name.frw": the name alone.
            effect.erase(0, effect.find_last_of("\\/") + 1);
            if (const auto dot = effect.rfind('.'); dot != std::string::npos)
                effect.resize(dot);
            const i64 kind = whole(entry, "EffectType");
            return {
                .kind = kind == 1 ? ShotKind::Laser
                    : kind == 2   ? ShotKind::WaterJet
                                  : ShotKind::Firework,
                .effect = std::move(effect),
                .launcher = launcher,
                .tube = static_cast<i32>(whole(entry, "SlotIndex")),
                .seconds = number(entry, "Time"),
                .lane = static_cast<i32>(whole(entry, "Track")),
            };
        }

        bool of_class(const save::SaveFile& file, const save::Object& o, std::string_view name)
        {
            return o.class_index < file.classes.size() && file.classes[o.class_index].name == name;
        }

        /// The object id of the description a park's laser or water jet shot `entry` keeps: its source names it by
        /// reference `field`.
        std::optional<u64> described(const save::SaveFile& file, const save::ObjectIndex& index, const save::Value& entry, std::string_view field)
        {
            const auto ref = entry.field("ParticleSourceRef");
            if (!ref || ref->kind() != save::ValueKind::Reference)
                return std::nullopt;
            const auto o = index.find(ref->as_reference());
            if (!o)
                return std::nullopt;
            const auto source = save::decode_object(file, *o);
            const auto desc = source ? source->fields.field(field) : std::nullopt;
            if (!desc || desc->kind() != save::ValueKind::Reference)
                return std::nullopt;
            return desc->as_reference();
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
                .col = static_cast<i32>(whole(entry, "Col")),
                .row = static_cast<i32>(whole(entry, "Row")),
                .corner = static_cast<i32>(whole(entry, "Corner")),
                .direction = static_cast<i32>(whole(entry, "Dir")),
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
            const i64 launcher = whole(entry, "SceneryEntry");
            if (launcher < 0 || static_cast<usize>(launcher) >= display.launchers.size())
                return std::unexpected(DisplayError::BadLauncher);
            display.shots.push_back(shot_of(entry, static_cast<usize>(launcher)));
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
                const u64 id = item->as_reference();
                auto launcher = std::ranges::find(display.launchers, std::optional(id), &Launcher::placed);
                if (launcher == display.launchers.end())
                    launcher = display.launchers.insert(launcher, Launcher { .placed = id });
                Shot& shot = display.shots.emplace_back(shot_of(entry, static_cast<usize>(launcher - display.launchers.begin())));
                if (const auto jet = shot.kind == ShotKind::WaterJet ? described(file, index, entry, "WaterJetBaseDesc") : std::nullopt)
                    shot.jet = water_jet_in(file, index, *jet);
                if (const auto laser = shot.kind == ShotKind::Laser ? described(file, index, entry, "LaserBaseDesc") : std::nullopt)
                    shot.laser = laser_in(file, index, *laser);
            }
            std::ranges::stable_sort(display.shots, { }, &Shot::seconds);
            displays.push_back(std::move(display));
        }
        return displays;
    }

    ShotKind main_kind(const Display& display)
    {
        std::array<usize, 3> counts { };
        for (const Shot& shot : display.shots)
            ++counts[static_cast<usize>(shot.kind)];
        return static_cast<ShotKind>(std::ranges::max_element(counts) - counts.begin());
    }

    f32 display_seconds(const Display& display)
    {
        f32 last = 0;
        for (const Shot& shot : display.shots)
            last = std::max(last, shot.seconds + (shot.jet ? shot.jet->seconds : shot.laser ? shot.laser->seconds : 0.0f));
        return last;
    }

    bool display_due(const Display& display, i32 month, f32 minutes)
    {
        const bool in_month = month >= 0 && month < 32 && (display.months >> month & 1u) != 0;
        const f32 since = minutes - static_cast<f32>(display.start_minutes);
        return in_month && since >= 0 && since < kStartWindowMinutes;
    }

    std::span<const Shot> advance_display(DisplayRun& run, const Display& display, f32 seconds)
    {
        run.seconds += seconds;
        const usize first = std::min(run.next_shot, display.shots.size());
        usize last = first;
        while (last < display.shots.size() && display.shots[last].seconds <= run.seconds)
            ++last;
        run.next_shot = last;
        return std::span(display.shots).subspan(first, last - first);
    }

} // namespace rct3::particles
