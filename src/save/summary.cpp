// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/summary.hpp"

#include "save/value.hpp"

#include <limits>
#include <optional>
#include <string_view>

namespace rct3::save
{

    namespace
    {

        // Class and attribute names of the summary, as the format names them.
        constexpr std::string_view kSummaryClass = "GUI2_DatDesc";
        constexpr std::string_view kCurrentClass = "GUI2_CurrentDatDesc";
        constexpr std::string_view kCurrent = "CurrDatDesc";

        std::optional<std::uint32_t> class_index(const SaveFile& file, std::string_view name)
        {
            for (std::size_t i = 0; i < file.classes.size(); ++i)
            {
                if (file.classes[i].name == name)
                    return static_cast<std::uint32_t>(i);
            }
            return std::nullopt;
        }

        std::int32_t integer(const DecodedObject& o, std::string_view name)
        {
            const Value* v = o.find(name);
            if (v == nullptr || v->kind() != ValueKind::Integer)
                return 0;
            const std::int64_t n = v->as_integer();
            if (n < std::numeric_limits<std::int32_t>::min() || n > std::numeric_limits<std::int32_t>::max())
                return 0;
            return static_cast<std::int32_t>(n);
        }

        std::uint8_t channel(const Value& pixel, std::string_view name)
        {
            const Value* v = pixel.field(name);
            return v != nullptr && v->kind() == ValueKind::Integer ? static_cast<std::uint8_t>(v->as_integer()) : 0;
        }

        std::vector<std::uint8_t> preview(const DecodedObject& o)
        {
            const Value* v = o.find("DatPreview");
            constexpr std::size_t kPixels = std::size_t { kPreviewWidth } * kPreviewHeight;
            if (v == nullptr || v->kind() != ValueKind::List || v->items().size() != kPixels)
                return { };
            std::vector<std::uint8_t> rgba;
            rgba.reserve(kPixels * 4);
            for (const Value& pixel : v->items())
            {
                rgba.push_back(channel(pixel, "R"));
                rgba.push_back(channel(pixel, "G"));
                rgba.push_back(channel(pixel, "B"));
                rgba.push_back(255);
            }
            return rgba;
        }

        std::vector<std::string> packs(const DecodedObject& o)
        {
            std::vector<std::string> names;
            const Value* v = o.find("AddOnPacksNeeded");
            if (v == nullptr || v->kind() != ValueKind::List)
                return names;
            for (const Value& pack : v->items())
            {
                const Value* name = pack.field("Name");
                if (name != nullptr && name->kind() == ValueKind::String)
                    names.emplace_back(name->text());
            }
            return names;
        }

        SaveSummary summary(const DecodedObject& o)
        {
            SaveSummary s;
            if (const Value* name = o.find("DatParkName"); name != nullptr && name->kind() == ValueKind::String)
                s.park_name = name->text();
            s.day = integer(o, "DatDay");
            s.month = integer(o, "DatMonth");
            s.year = integer(o, "DatYear");
            s.minutes = integer(o, "DatMinutesSinceMidnight");
            s.guests = integer(o, "DatGuestCountInt");
            s.kind = integer(o, "DatType");
            s.preview = preview(o);
            s.packs = packs(o);
            return s;
        }

        /// The summary the file names as its own.
        std::optional<DecodedObject> named_summary(const SaveFile& file, std::uint32_t summary_class)
        {
            const auto current_class = class_index(file, kCurrentClass);
            if (!current_class)
                return std::nullopt;
            for (const Object& o : file.objects)
            {
                if (o.class_index != *current_class)
                    continue;
                const auto current = decode_object(file, o);
                const Value* ref = current ? current->find(kCurrent) : nullptr;
                if (ref == nullptr || ref->kind() != ValueKind::Reference)
                    return std::nullopt;
                const Object* target = ObjectIndex(file).find(ref->as_reference());
                if (target == nullptr || target->class_index != summary_class)
                    return std::nullopt;
                auto decoded = decode_object(file, *target);
                return decoded ? std::optional(std::move(*decoded)) : std::nullopt;
            }
            return std::nullopt;
        }

    } // namespace

    const char* to_string(SummaryError e) noexcept
    {
        switch (e)
        {
            case SummaryError::NotASave:
                return "not a save";
            case SummaryError::NoSummary:
                return "no summary in the save";
        }
        return "unknown";
    }

    std::expected<SaveSummary, SummaryError> read_summary(const SaveFile& file)
    {
        const auto summary_class = class_index(file, kSummaryClass);
        if (!summary_class)
            return std::unexpected(SummaryError::NoSummary);
        if (const auto named = named_summary(file, *summary_class))
            return summary(*named);
        for (const Object& o : file.objects)
        {
            if (o.class_index != *summary_class)
                continue;
            const auto decoded = decode_object(file, o);
            if (decoded && integer(*decoded, "DatType") == 0)
                return summary(*decoded);
        }
        return std::unexpected(SummaryError::NoSummary);
    }

    std::expected<SaveSummary, SummaryError> read_summary(std::span<const std::byte> data)
    {
        const auto file = read_save(data, { .verify_checksum = false });
        if (!file)
            return std::unexpected(SummaryError::NotASave);
        return read_summary(*file);
    }

} // namespace rct3::save
