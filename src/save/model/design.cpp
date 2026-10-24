// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/design.hpp"

#include "save/value.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace rct3::save
{

    namespace
    {

        // Class and attribute names of a design's header and picture, as the format names them.
        constexpr std::string_view kHeaderClass = "TrackedRideInstanceSaveHeader";
        constexpr std::string_view kPicture = "TrackedRidePreview";
        constexpr std::uint32_t kLargestPicture = 1024; ///< A bound on what a file may claim
        constexpr std::uint8_t kLevels = 63;            ///< Levels of a packed pixel
        constexpr std::uint8_t kFirstSkip = 64;         ///< Packed bytes from here skip clear pixels

        std::int32_t integer(const Value& v, std::string_view name, std::int32_t fallback = 0)
        {
            const auto f = v.field(name);
            if (!f || f->kind() != ValueKind::Integer)
                return fallback;
            return static_cast<std::int32_t>(std::clamp<std::int64_t>(f->as_integer(), std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
        }

        float number(const Value& v, std::string_view name)
        {
            const auto f = v.field(name);
            if (!f)
                return 0;
            const float x = f->kind() == ValueKind::Float ? f->as_float() : f->kind() == ValueKind::Integer ? static_cast<float>(f->as_integer()) : 0.0f;
            return std::isfinite(x) ? x : 0.0f;
        }

        std::vector<std::uint8_t> bytes(const Value& v, std::string_view name)
        {
            std::vector<std::uint8_t> out;
            const auto f = v.field(name);
            if (!f || f->kind() != ValueKind::List)
                return out;
            out.reserve(f->items().size());
            for (const Value& item : f->items())
                out.push_back(item.kind() == ValueKind::Integer ? static_cast<std::uint8_t>(item.as_integer()) : 0);
            return out;
        }

    } // namespace

    const char* to_string(DesignError e) noexcept
    {
        switch (e)
        {
            case DesignError::NotASave:
                return "not a save";
            case DesignError::NoHeader:
                return "no design header in the save";
        }
        return "unknown";
    }

    std::vector<std::uint8_t> design_picture(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> packed, std::span<const std::uint8_t> unpacked)
    {
        const std::size_t pixels = std::size_t { width } * height;
        if (width == 0 || height == 0 || width > kLargestPicture || height > kLargestPicture)
            return { };
        std::vector<std::uint8_t> rgba(pixels * 4, 0);
        const auto put = [&](std::size_t at, std::uint8_t level) {
            rgba[at * 4] = rgba[at * 4 + 1] = rgba[at * 4 + 2] = level;
            rgba[at * 4 + 3] = 255;
        };
        if (packed.empty())
        {
            if (unpacked.size() != pixels)
                return { };
            for (std::size_t i = 0; i < pixels; ++i)
                if (unpacked[i] != 0)
                    put(i, unpacked[i]);
            return rgba;
        }
        std::size_t at = 0;
        for (std::size_t i = 0; i < packed.size(); ++i)
        {
            const std::uint8_t b = packed[i];
            if (b == 0)
            {
                if (i + 1 < packed.size() && packed[i + 1] == 0)
                    break;
                ++i; // a lone zero takes the byte after it along
            }
            else if (b < kFirstSkip)
            {
                if (at < pixels)
                    put(at, static_cast<std::uint8_t>(std::lround(static_cast<float>(b) * 255.0f / kLevels)));
                ++at;
            }
            else
                at += b - (kFirstSkip - 1);
        }
        return rgba;
    }

    std::expected<DesignSummary, DesignError> read_design_summary(std::span<const std::byte> data)
    {
        const auto file = read_save(data, { .verify_checksum = false });
        if (!file)
            return std::unexpected(DesignError::NotASave);
        std::optional<std::uint32_t> header_class;
        for (std::size_t i = 0; i < file->classes.size() && !header_class; ++i)
            if (file->classes[i].name == kHeaderClass)
                header_class = static_cast<std::uint32_t>(i);
        const auto object = header_class ? std::ranges::find(file->objects, *header_class, &Object::class_index) : file->objects.end();
        if (object == file->objects.end())
            return std::unexpected(DesignError::NoHeader);
        const auto header = decode_object(*file, *object);
        if (!header)
            return std::unexpected(DesignError::NoHeader);
        const Value& h = header->fields;
        DesignSummary s;
        if (const auto symbol = h.field("TrackedRideSymbolName"); symbol && symbol->kind() == ValueKind::String)
            s.ride_symbol = symbol->text();
        s.width = integer(h, "Cols");
        s.length = integer(h, "Rows");
        if (const auto cost = h.field("Cost"); cost && cost->kind() == ValueKind::Integer)
            s.cost = cost->as_integer();
        s.scenario = integer(h, "NeedScenarioComplete", -1);
        s.level = integer(h, "CompletionLevel");
        s.train = integer(h, "TrainSelection");
        if (const auto a = h.field("RideAssessmentResults"); a && a->kind() == ValueKind::Struct)
        {
            const auto valid = a->field("AssessmentIsValid");
            const auto ratings = a->field("AssessmentRatings");
            if (valid && valid->as_bool() && ratings && ratings->floats().size() == 3)
                s.assessment = { .valid = true, .excitement = ratings->floats()[0], .intensity = ratings->floats()[1], .nausea = ratings->floats()[2], .value = number(*a, "AssessmentValue") };
        }
        if (const auto r = h.field("TestResults"); r && r->kind() == ValueKind::Struct && (integer(*r, "Valid") & 1) != 0)
            s.results = TestResults {
                .max_speed = number(*r, "MaxSpeed"),
                .average_speed = number(*r, "AverageSpeed"),
                .ride_time = number(*r, "RideTime"),
                .ride_length = number(*r, "RideLength"),
                .max_positive_vertical = number(*r, "MaxPositiveVerticalG"),
                .max_negative_vertical = number(*r, "MaxNegativeVerticalG"),
                .max_lateral = number(*r, "MaxLateralG"),
                .air_time = number(*r, "AirTime"),
                .drop_count = integer(*r, "NDrops"),
                .highest_drop = number(*r, "HighestDropHeight"),
                .inversion_count = integer(*r, "NInversions"),
                .lift_hill_count = integer(*r, "NLiftHills"),
                .valid = true,
            };
        if (const auto ref = h.field(kPicture); ref && ref->kind() == ValueKind::Reference)
            if (const auto target = ObjectIndex(*file).find(ref->as_reference()))
                if (const auto picture = decode_object(*file, *target))
                {
                    const Value& p = picture->fields;
                    const auto width = static_cast<std::uint32_t>(std::max(integer(p, "Width"), 0));
                    const auto height = static_cast<std::uint32_t>(std::max(integer(p, "Height"), 0));
                    s.picture = design_picture(width, height, bytes(p, "Compact"), bytes(p, "Red"));
                    if (!s.picture.empty())
                    {
                        s.picture_width = width;
                        s.picture_height = height;
                    }
                }
        return s;
    }

} // namespace rct3::save
