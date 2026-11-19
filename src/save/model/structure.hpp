// SPDX-License-Identifier: GPL-3.0-or-later
//
// A saved scenery structure: a group of scenery items the player picked out of a park to build again
// elsewhere. The original game keeps each one as a file of the park save's container in its "Structures"
// folder, one structure a file, with its name, the box of tiles it covers, which tiles of that box belong to
// it, and each item: the tile it stands on counted from the box's corner, its height, quarter turns, quarter
// of the tile and colours, and the scenery entry it is made from.
//
// Heights in the original files are the park's own, wherever the structure was picked; they are brought down
// here so the lowest item stands at 0, and placing adds the height of the ground it goes on.
#pragma once

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace rct3::save::model
{

    /// One scenery item of a structure.
    struct StructurePiece
    {
        std::string symbol { };  ///< Of the scenery entry
        std::string overlay { }; ///< The overlay that holds it, as placed objects name it (world.hpp)
        i32 col = 0;             ///< Counted from the box's first column
        i32 row = 0;             ///< ... and row
        i32 height = 0;          ///< Above the structure's lowest item
        i32 rotation = 0;
        i32 quadrant = 0;
        std::array<i32, 3> colours { };
    };

    struct Structure
    {
        std::string name { };
        i32 cols = 0;
        i32 rows = 0;
        std::vector<u8> tiles { }; ///< cols x rows, row after row: 1 where the tile belongs to it
        std::vector<StructurePiece> pieces { };

        [[nodiscard]] bool has_tile(i32 col, i32 row) const noexcept
        {
            return col >= 0 && row >= 0 && col < cols && row < rows && tiles[static_cast<usize>(row) * static_cast<usize>(cols) + static_cast<usize>(col)] != 0;
        }
    };

    enum class StructureError
    {
        NotASave,    ///< The container does not read
        NoStructure, ///< A save without a structure in it
    };

    [[nodiscard]] const char* to_string(StructureError e) noexcept;

    /// The structure an original structure file holds. Items whose scenery entry does not resolve are left out.
    [[nodiscard]] std::expected<Structure, StructureError> read_structure(std::span<const std::byte> data);

    /// Brings the heights down so the lowest item stands at 0.
    void rebase_heights(Structure& structure);

} // namespace rct3::save::model
