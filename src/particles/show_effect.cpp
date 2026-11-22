// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/show_effect.hpp"

#include "core/types.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace rct3::particles
{

    namespace
    {

        // A description is kept under either of two class names, with the same fields.
        constexpr std::array<std::string_view, 2> kJetClasses { "WaterJetDesc", "WaterJetBaseDesc" };
        constexpr std::array<std::string_view, 2> kLaserClasses { "LaserDesc", "LaserBaseDesc" };
        constexpr std::array<std::string_view, 1> kDrawingClasses { "LaserWritingLetter" };

        f32 number(const save::Value& parent, std::string_view name)
        {
            const auto v = parent.field(name);
            if (v && v->kind() == save::ValueKind::Float)
                return v->as_float();
            return v && v->kind() == save::ValueKind::Integer ? static_cast<f32>(v->as_integer()) : 0.0f;
        }

        i64 whole(const save::Value& parent, std::string_view name)
        {
            const auto v = parent.field(name);
            return v && v->kind() == save::ValueKind::Integer ? v->as_integer() : 0;
        }

        std::span<const save::Value> list(const save::Value& parent, std::string_view name)
        {
            const auto v = parent.field(name);
            return v && v->kind() == save::ValueKind::List ? v->items() : std::span<const save::Value> { };
        }

        bool of_class(const save::SaveFile& file, const save::Object& o, std::span<const std::string_view> names)
        {
            return o.class_index < file.classes.size() && std::ranges::contains(names, file.classes[o.class_index].name);
        }

        /// Object `id`, decoded, when it is of one of `classes`.
        std::optional<save::DecodedObject> object(const save::SaveFile& file, const save::ObjectIndex& index, u64 id, std::span<const std::string_view> classes)
        {
            const auto o = index.find(id);
            if (!o || !of_class(file, *o, classes))
                return std::nullopt;
            auto decoded = save::decode_object(file, *o);
            return decoded ? std::optional(std::move(*decoded)) : std::nullopt;
        }

        /// The object `parent`'s reference `name` points to, whatever its class.
        std::optional<save::DecodedObject> referred(const save::SaveFile& file, const save::ObjectIndex& index, const save::Value& parent, std::string_view name)
        {
            const auto ref = parent.field(name);
            if (!ref || ref->kind() != save::ValueKind::Reference)
                return std::nullopt;
            const auto o = index.find(ref->as_reference());
            if (!o)
                return std::nullopt;
            auto decoded = save::decode_object(file, *o);
            return decoded ? std::optional(std::move(*decoded)) : std::nullopt;
        }

        struct TimelinePoint
        {
            f32 time = 0; ///< 0 to 1 over the timeline
            save::DecodedObject element { };
        };

        /// The control points in use of timeline `name` of `desc`, earliest first: files keep them in the order they
        /// were added.
        std::vector<TimelinePoint> timeline(const save::SaveFile& file, const save::ObjectIndex& index, const save::Value& desc, std::string_view name)
        {
            std::vector<TimelinePoint> points;
            const auto blender = referred(file, index, desc, name);
            if (!blender)
                return points;
            const f32 longest = number(blender->fields, "MaxTime");
            const auto stored = list(blender->fields, "FixedPts");
            // Past the count the slots hold leftovers, not points.
            const usize used = static_cast<usize>(std::clamp<i64>(whole(blender->fields, "NumPts"), 0, static_cast<i64>(stored.size())));
            for (const save::Value& point : stored.first(used))
            {
                const f32 time = longest > 0 ? number(point, "PointTime") / longest : 0.0f;
                // A time that is not a number would leave the points unorderable.
                if (auto element = std::isfinite(time) ? referred(file, index, point, "BlenderElement") : std::nullopt)
                    points.push_back({ .time = time, .element = std::move(*element) });
            }
            std::ranges::stable_sort(points, { }, &TimelinePoint::time);
            return points;
        }

        Curve numbers(const save::SaveFile& file, const save::ObjectIndex& index, const save::Value& desc, std::string_view name)
        {
            Curve c;
            for (const TimelinePoint& point : timeline(file, index, desc, name))
            {
                c.times.push_back(point.time);
                c.values.push_back(number(point.element.fields, "Float"));
            }
            return c;
        }

        /// A colour timeline: each point a colour packed opacity, red, green, blue from the highest byte down.
        std::array<Curve, 4> colours(const save::SaveFile& file, const save::ObjectIndex& index, const save::Value& desc)
        {
            std::array<Curve, 4> rgba;
            for (const TimelinePoint& point : timeline(file, index, desc, "ColourBlender"))
            {
                const u32 argb = static_cast<u32>(whole(point.element.fields, "Colour"));
                for (const auto [channel, shift] : { std::pair { 0, 16 }, { 1, 8 }, { 2, 0 }, { 3, 24 } })
                {
                    rgba[channel].times.push_back(point.time);
                    rgba[channel].values.push_back(static_cast<f32>(argb >> shift & 0xff) / 255.0f);
                }
            }
            return rgba;
        }

        LaserDrawing drawing_of(const save::Value& letter)
        {
            const auto stored = list(letter, "DragPoints");
            const auto lit = list(letter, "SolidConnection");
            const usize used = static_cast<usize>(std::clamp<i64>(whole(letter, "NumPts"), 0, static_cast<i64>(stored.size())));
            LaserDrawing drawing { .seconds = number(letter, "Duration") };
            for (usize i = 0; i < used; ++i)
                drawing.points.push_back({ .at = { number(stored[i], "X"), number(stored[i], "Y") }, .lit = i < lit.size() && lit[i].as_bool() });
            return drawing;
        }

        struct Root
        {
            save::SaveFile file { };
            u64 id = 0;
        };

        /// The container `data` and the first object in it of one of `classes`, for the readers of loose files.
        std::expected<Root, ShowEffectError> root_of(std::span<const std::byte> data, std::span<const std::string_view> classes)
        {
            auto file = save::read_save(data, { .verify_checksum = false });
            if (!file)
                return std::unexpected(ShowEffectError::Container);
            const auto root = std::ranges::find_if(file->objects, [&](const save::Object& o) { return of_class(*file, o, classes); });
            if (root == file->objects.end())
                return std::unexpected(ShowEffectError::NoEffect);
            const u64 id = root->id;
            return Root { .file = std::move(*file), .id = id };
        }

    } // namespace

    const char* to_string(ShowEffectError e) noexcept
    {
        switch (e)
        {
            case ShowEffectError::Container:
                return "not a readable container";
            case ShowEffectError::NoEffect:
                return "no effect of that kind in it";
            case ShowEffectError::BadObject:
                return "an object could not be decoded";
        }
        return "unknown";
    }

    std::optional<JetPattern> water_jet_in(const save::SaveFile& file, const save::ObjectIndex& index, u64 id)
    {
        const auto desc = object(file, index, id, kJetClasses);
        if (!desc)
            return std::nullopt;
        const save::Value& fields = desc->fields;
        const auto text = fields.field("Name");
        return JetPattern {
            .name = text && text->kind() == save::ValueKind::String ? std::string(text->text()) : std::string { },
            .seconds = number(fields, "Duration"),
            .pressure = numbers(file, index, fields, "PressureBlender"),
            .elevation = numbers(file, index, fields, "ElevationBlender"),
            .widening = numbers(file, index, fields, "DiameterDeltaBlender"),
            .colour = colours(file, index, fields),
        };
    }

    std::optional<Laser> laser_in(const save::SaveFile& file, const save::ObjectIndex& index, u64 id)
    {
        const auto desc = object(file, index, id, kLaserClasses);
        if (!desc)
            return std::nullopt;
        const save::Value& fields = desc->fields;
        const auto text = fields.field("Name");
        const i64 look = whole(fields, "RenderType");
        Laser laser {
            .name = text && text->kind() == save::ValueKind::String ? std::string(text->text()) : std::string { },
            .seconds = number(fields, "Duration"),
            .look = look == 1 ? LaserLook::Plane
                : look == 2   ? LaserLook::Drawing
                              : LaserLook::Cone,
            .beams = static_cast<i32>(whole(fields, "NumBeams")),
            .power = number(fields, "Power"),
            .trail_seconds = number(fields, "TrailDuration"),
            .drawing_scale = number(fields, "ImageSpread"),
            .colour = colours(file, index, fields),
            .elevation = numbers(file, index, fields, "ElevationBlender"),
            .spin = numbers(file, index, fields, "RotVelBlender"),
            .width = numbers(file, index, fields, "SpreadBlender"),
        };
        if (const auto word = referred(file, index, fields, "LaserWord"))
            for (const save::Value& ref : list(word->fields, "Letters"))
                if (ref.kind() == save::ValueKind::Reference)
                    if (const auto letter = object(file, index, ref.as_reference(), kDrawingClasses))
                        laser.drawings.push_back(drawing_of(letter->fields));
        return laser;
    }

    std::expected<JetPattern, ShowEffectError> read_water_jet(std::span<const std::byte> data)
    {
        const auto root = root_of(data, kJetClasses);
        if (!root)
            return std::unexpected(root.error());
        auto jet = water_jet_in(root->file, save::ObjectIndex(root->file), root->id);
        if (!jet)
            return std::unexpected(ShowEffectError::BadObject);
        return std::move(*jet);
    }

    std::expected<Laser, ShowEffectError> read_laser(std::span<const std::byte> data)
    {
        const auto root = root_of(data, kLaserClasses);
        if (!root)
            return std::unexpected(root.error());
        auto laser = laser_in(root->file, save::ObjectIndex(root->file), root->id);
        if (!laser)
            return std::unexpected(ShowEffectError::BadObject);
        return std::move(*laser);
    }

    std::expected<LaserDrawing, ShowEffectError> read_laser_drawing(std::span<const std::byte> data)
    {
        const auto root = root_of(data, kDrawingClasses);
        if (!root)
            return std::unexpected(root.error());
        const auto letter = object(root->file, save::ObjectIndex(root->file), root->id, kDrawingClasses);
        if (!letter)
            return std::unexpected(ShowEffectError::BadObject);
        return drawing_of(letter->fields);
    }

} // namespace rct3::particles
