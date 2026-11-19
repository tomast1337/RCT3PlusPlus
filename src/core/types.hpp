// SPDX-License-Identifier: GPL-3.0-or-later
//
// Short names for the number types, as in Rust: the width of a number is in its name, the same on every
// compiler and platform. They are global so every file uses them unqualified.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using usize = std::size_t;
using f32 = float; // Becomes std::float32_t once Clang has it (#338).
using f64 = double;

// The code reads f32 as a 32-bit IEEE 754 number and f64 as a 64-bit one; a platform where that does not hold
// fails to build here.
static_assert(std::numeric_limits<f32>::is_iec559 && sizeof(f32) == 4, "f32 must be 32-bit IEEE 754");
static_assert(std::numeric_limits<f64>::is_iec559 && sizeof(f64) == 8, "f64 must be 64-bit IEEE 754");
