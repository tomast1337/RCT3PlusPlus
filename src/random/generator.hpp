// SPDX-License-Identifier: GPL-3.0-or-later
//
// Our random numbers: xoshiro256** (Blackman and Vigna's public-domain generator), with its 256 bits of
// state filled from a 64-bit seed by SplitMix64. One seed gives several named streams that never draw from
// each other, so placing scenery does not change what the gameplay rolls next, and the other way round.
//
// The state is a plain value: copy it to branch, compare it, and write it out as text to keep it with a
// saved park, so a park and a seed replay the same way.
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace rct3::random
{

    struct RandomState
    {
        std::array<std::uint64_t, 4> words { };

        bool operator==(const RandomState&) const = default;
    };

    /// What each stream is for. A stream's number is part of the format of saved states: never reorder.
    enum class RandomStream : std::uint64_t
    {
        Gameplay = 1, ///< Anything that changes how the park plays: guests, rides, weather, research
        Scenery = 2,  ///< Placing generated scenery, such as the trees beyond the map
        Effects = 3,  ///< Looks only: particles, sound variations; never saved
    };

    /// One step of SplitMix64: advances `state` and returns a well-mixed value. Also good for mixing
    /// several numbers into one seed.
    [[nodiscard]] std::uint64_t split_mix(std::uint64_t& state);

    /// A state seeded from `seed`. Any seed, zero too, gives a usable state.
    [[nodiscard]] RandomState seeded(std::uint64_t seed);
    /// The state of stream `stream` of `seed`.
    [[nodiscard]] RandomState stream(std::uint64_t seed, RandomStream stream);

    /// The next 64 random bits.
    [[nodiscard]] std::uint64_t next(RandomState& state);
    /// A value in [0, 1).
    [[nodiscard]] double unit(RandomState& state);
    /// A whole number in [0, count), every one equally likely; 0 when `count` is 0.
    [[nodiscard]] std::uint64_t below(RandomState& state, std::uint64_t count);
    /// A whole number in [low, high], both included; `low` when `high` is below it.
    [[nodiscard]] std::int64_t between(RandomState& state, std::int64_t low, std::int64_t high);
    /// A value in [low, high).
    [[nodiscard]] double between(RandomState& state, double low, double high);
    /// True with probability `p` (0 never, 1 always).
    [[nodiscard]] bool chance(RandomState& state, double p);

    /// A seed as written for people: "0x" and 16 hex digits, which parse_seed reads back.
    [[nodiscard]] std::string seed_text(std::uint64_t seed);
    /// A seed given as decimal digits or as "0x" and hex digits, from a command line or a test.
    [[nodiscard]] std::expected<std::uint64_t, std::string> parse_seed(std::string_view text);

    /// The state as 64 lower-case hex digits, the four words in order.
    [[nodiscard]] std::string to_text(const RandomState& state);
    /// A state written by to_text. All zero words are refused: the generator would stay at zero.
    [[nodiscard]] std::expected<RandomState, std::string> parse_state(std::string_view text);

} // namespace rct3::random
