// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/structure.hpp"

#include "core/types.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>

namespace rct3::save::model
{

    namespace
    {

        // Names of the structure's class and attributes, as the format names them.
        constexpr std::string_view kStructureClass = "SavedSelection";
        constexpr i32 kLargestSide = 4096; ///< A bound on the box a file may claim

        i32 integer(const Value& v, std::string_view name)
        {
            const auto f = v.field(name);
            if (!f || f->kind() != ValueKind::Integer)
                return 0;
            return static_cast<i32>(std::clamp<i64>(f->as_integer(), std::numeric_limits<i32>::min(), std::numeric_limits<i32>::max()));
        }

        std::string text(const Value& v, std::string_view name)
        {
            const auto f = v.field(name);
            return f && f->kind() == ValueKind::String ? std::string(f->text()) : std::string();
        }

    } // namespace

    const char* to_string(StructureError e) noexcept
    {
        switch (e)
        {
            case StructureError::NotASave:
                return "not a save";
            case StructureError::NoStructure:
                return "no structure in the save";
        }
        return "unknown";
    }

    void rebase_heights(Structure& structure)
    {
        if (structure.pieces.empty())
            return;
        const i32 lowest = std::ranges::min(structure.pieces, { }, &StructurePiece::height).height;
        for (StructurePiece& p : structure.pieces)
            p.height -= lowest;
    }

    std::expected<Structure, StructureError> read_structure(std::span<const std::byte> data)
    {
        const auto file = read_save(data, { .verify_checksum = false });
        if (!file)
            return std::unexpected(StructureError::NotASave);
        std::optional<u32> cls;
        for (usize i = 0; i < file->classes.size() && !cls; ++i)
            if (file->classes[i].name == kStructureClass)
                cls = static_cast<u32>(i);
        const auto object = cls ? std::ranges::find(file->objects, *cls, &Object::class_index) : file->objects.end();
        if (object == file->objects.end())
            return std::unexpected(StructureError::NoStructure);
        const auto decoded = decode_object(*file, *object);
        if (!decoded)
            return std::unexpected(StructureError::NoStructure);
        const Value& s = decoded->fields;

        Structure out;
        out.name = text(s, "Name");
        out.cols = integer(s, "Cols");
        out.rows = integer(s, "Rows");
        if (out.cols <= 0 || out.rows <= 0 || out.cols > kLargestSide || out.rows > kLargestSide)
            return std::unexpected(StructureError::NoStructure);
        const usize tiles = static_cast<usize>(out.cols) * static_cast<usize>(out.rows);
        out.tiles.assign(tiles, 0);
        if (const auto selected = s.field("Selected"); selected && selected->kind() == ValueKind::List)
        {
            const auto items = selected->items();
            for (usize i = 0; i < std::min(tiles, items.size()); ++i)
                out.tiles[i] = items[i].as_bool() ? 1 : 0;
        }

        const ObjectIndex index(*file);
        if (const auto scenery = s.field("Scenery"); scenery && scenery->kind() == ValueKind::List)
            for (const Value& item : scenery->items())
            {
                const auto sid = item.field("SID");
                if (!sid || sid->kind() != ValueKind::Reference)
                    continue;
                const auto entry_object = index.find(sid->as_reference());
                if (!entry_object)
                    continue;
                const auto entry = decode_object(*file, *entry_object);
                if (!entry)
                    continue;
                StructurePiece p {
                    .symbol = text(entry->fields, "SYMBOLNAME"),
                    .overlay = text(entry->fields, "OVERLAYFILENAME"),
                    .col = integer(item, "Col"),
                    .row = integer(item, "Row"),
                    .height = integer(item, "Height"),
                    .rotation = integer(item, "Dir") & 3,
                    .quadrant = integer(item, "Corner") & 3,
                    .colours = { integer(item, "FlexiPick0"), integer(item, "FlexiPick1"), integer(item, "FlexiPick2") },
                };
                if (!p.symbol.empty())
                    out.pieces.push_back(std::move(p));
            }
        // shortcut: original heights are taken as the park's own and brought down to the lowest item; check
        // against a structure the original game saved on raised ground once one is at hand.
        rebase_heights(out);
        return out;
    }

} // namespace rct3::save::model
