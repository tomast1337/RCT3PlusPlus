// SPDX-License-Identifier: GPL-3.0-or-later
//
// Our own park file: what the simulation needs to carry on from where it was saved. Original saves are only
// ever read (see save/); parks are written, and read back, in this format.
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
//   "WRLD" v1  tick u64, seed u64
//   "ENTS" v1  count u32, then each entity's id u32, ascending
//   "VSTR" v1  Visitor:  count u32, then entity u32, age u8, female u8, name (u32 length, UTF-8), arrived u64
//   "POSE" v1  Pose:     count u32, then entity u32, position 3 x f32, heading f32
//   "NEED" v1  Needs:    count u32, then entity u32, hunger, thirst, bladder f32
//   "MOOD" v1  Mood:     count u32, then entity u32, happiness, energy, nausea f32
//   "WALT" v1  Wallet:   count u32, then entity u32, cash i64
//   "DPRT" v1  Departure: count u32, then entity u32, load waited for u8 (0 full, 1 three quarters, 2 half,
//              3 quarter, 4 any), least and longest wait i32 seconds (-1 none)
//
// Component records are in ascending entity order and name only entities listed in "ENTS". A reader skips
// chunks whose tag it does not know, so newer files with extra chunks still load; it refuses a chunk whose
// version is newer than it knows, and a file whose version is newer than its own. Writing what was read
// gives the same bytes.
//
// Parks may grow past what the original game allows, so nothing here is sized to its limits: counts and
// entity ids are u32, chunk lengths u64, and map coordinates (when chunks for terrain and paths come) are
// to be at least u16 per axis, never the original's single byte.
#pragma once

#include "simulation/sim_world.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace rct3::parkfile
{

    inline constexpr std::uint32_t kParkFileVersion = 1;

    [[nodiscard]] std::vector<std::byte> write_park_file(const simulation::SimWorld& world);

    /// The world `bytes` hold, or why they do not hold one.
    [[nodiscard]] std::expected<simulation::SimWorld, std::string> read_park_file(std::span<const std::byte> bytes);

} // namespace rct3::parkfile
