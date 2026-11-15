// SPDX-License-Identifier: GPL-3.0-or-later
//
// The constants of the small, well known hashes used where a steady number is wanted from a name or an id: the
// same input always gives the same number, and nearby inputs give numbers far apart.
#pragma once

#include <cstdint>

namespace rct3::core
{

    /// FNV-1a over 32 bits: start at the offset, then for each byte xor it in and multiply by the prime.
    inline constexpr std::uint32_t kFnv32Offset = 2166136261u;
    inline constexpr std::uint32_t kFnv32Prime = 16777619u;
    /// FNV-1a over 64 bits.
    inline constexpr std::uint64_t kFnv64Offset = 14695981039346656037u;
    inline constexpr std::uint64_t kFnv64Prime = 1099511628211u;
    /// Knuth's multiplicative hash: 2^32 over the golden ratio, so consecutive ids land far apart.
    inline constexpr std::uint32_t kGoldenSpread = 2654435761u;

} // namespace rct3::core
