// SPDX-License-Identifier: GPL-3.0-or-later
//
// Our own structure file: a group of scenery items the player saved to build again (see
// save/model/structure.hpp, which reads the original game's). It is a park file (see park_file.hpp) with one
// chunk, a layout hash then the value field by field (see binary.hpp), so it is a ".park" file in the structures
// folder, listed by the same save window as parks:
//
//   "STRC" v1  save::Structure, heights above its lowest item
//
// shortcut: no picture for the list of structures, which shows names only; add one when the list draws them.
#pragma once

#include "save/model/structure.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace rct3::parkfile
{

    [[nodiscard]] std::vector<std::byte> write_structure_file(const save::Structure& structure);

    /// The structure `bytes` hold, or why they do not hold one.
    [[nodiscard]] std::expected<save::Structure, std::string> read_structure_file(std::span<const std::byte> bytes);

} // namespace rct3::parkfile
