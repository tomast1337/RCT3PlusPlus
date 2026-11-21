// SPDX-License-Identifier: GPL-3.0-or-later
//
// Our own park file: the simulated park whole, so a park read back steps on exactly as the one written would have.
// Original saves are only ever read (see save/); parks are written, and read back, in this format.
//
// Layout, all little-endian:
//
//   magic       8 bytes, "R3PPPARK"
//   version     u32, the file format version (kParkFileVersion when written)
//   chunk count u32
//   chunks      each: tag (4 ASCII bytes), version u32, length u64, then `length` bytes of payload
//
// Chunks, in the order written:
//
//   "WRLD" v2  the run, field by field (binary.hpp): tick, seed, gameplay rolls, guests' slow motion, whether
//              parties stand together often, and the cheats on
//   "ENTS" v2  every entity the park has made: count u32, how many of them are in use u32, then each one's id u32
//              (with its version) in the order the park keeps them, those in use first. The rest were destroyed,
//              and are handed out again, in that order, before new ones.
//   "PLNK" v1  the links across the park that are not walked (rides between stations), field by field
//   then one chunk for each component (simulation/component_tags.hpp names them and their tags), v1: a u64 hash of
//              the component's layout, a count u32, then each record's entity u32 and (unless the component holds
//              nothing) its value field by field, in the order the park keeps them
//   and one for each park-wide value the park holds, v1: as put_chunk writes it (binary.hpp)
//
// Records name only entities in use. A reader skips chunks whose tag it does not know, so newer files with extra
// chunks still load; it refuses a chunk whose version it does not read, or whose layout differs from its own, and
// a file whose version is newer than its own. Writing what was read gives the same bytes.
//
// A park the player saves uses this container with chunks of its own, and keeps one of these inside; see
// saved_park.hpp.
//
// Parks may grow past what the original game allows, so nothing here is sized to its limits: counts and
// entity ids are u32, chunk lengths u64, and map coordinates (when chunks for terrain and paths come) are
// to be at least u16 per axis, never the original's single byte.
#pragma once

#include "core/types.hpp"
#include "simulation/sim_world.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace rct3::parkfile
{

    inline constexpr u32 kParkFileVersion = 1;

    [[nodiscard]] std::vector<std::byte> write_park_file(const simulation::SimWorld& world);

    /// The world `bytes` hold, or why they do not hold one.
    [[nodiscard]] std::expected<simulation::SimWorld, std::string> read_park_file(std::span<const std::byte> bytes);

    /// Puts the entities, park-wide values, run and path links `bytes` hold into `world` in place of its own,
    /// keeping its paths and rating model; or says why they do not hold a world, leaving it part read.
    [[nodiscard]] std::expected<void, std::string> read_park_file(std::span<const std::byte> bytes, simulation::SimWorld& world);

} // namespace rct3::parkfile
