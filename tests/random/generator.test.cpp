// SPDX-License-Identifier: GPL-3.0-or-later
#include "random/generator.hpp"

#include <algorithm>
#include <array>
#include <doctest/doctest.h>

using namespace rct3::random;

TEST_CASE("the generators give their published sequences")
{
    // SplitMix64 from 0, and xoshiro256** from the words 1, 2, 3, 4.
    std::uint64_t counter = 0;
    CHECK(split_mix(counter) == 0xe220a8397b1dcdafULL);
    CHECK(split_mix(counter) == 0x6e789e6aa1b965f4ULL);
    RandomState s { { 1, 2, 3, 4 } };
    CHECK(next(s) == 11520ULL);
    CHECK(next(s) == 0ULL);
    CHECK(next(s) == 1509978240ULL);
    CHECK(next(s) == 1215971899390074240ULL);
}

TEST_CASE("a seed always gives the same numbers, and each stream its own")
{
    RandomState a = seeded(42);
    RandomState b = seeded(42);
    for (int i = 0; i < 10; ++i)
        CHECK(next(a) == next(b));
    CHECK(seeded(0) != RandomState { });
    CHECK(stream(42, RandomStream::Gameplay) == stream(42, RandomStream::Gameplay));
    CHECK(stream(42, RandomStream::Gameplay) != stream(42, RandomStream::Scenery));
    CHECK(stream(42, RandomStream::Gameplay) != stream(43, RandomStream::Gameplay));
}

TEST_CASE("draws stay in their ranges")
{
    RandomState s = seeded(7);
    std::array<int, 6> seen { };
    for (int i = 0; i < 6000; ++i)
    {
        const double u = unit(s);
        CHECK(u >= 0);
        CHECK(u < 1);
        const auto face = between(s, std::int64_t { 1 }, std::int64_t { 6 });
        REQUIRE(face >= 1);
        REQUIRE(face <= 6);
        ++seen[static_cast<std::size_t>(face - 1)];
    }
    // Each face about a sixth of the time.
    CHECK(std::ranges::all_of(seen, [](int n) { return n > 850 && n < 1150; }));
    CHECK(below(s, 0) == 0);
    CHECK(below(s, 1) == 0);
    CHECK(between(s, std::int64_t { 5 }, std::int64_t { 2 }) == 5);
    CHECK(between(s, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()) != 0); // Whole range, any value
    CHECK_FALSE(chance(s, 0));
    CHECK(chance(s, 1));
    const double r = between(s, 2.0, 3.0);
    CHECK(r >= 2);
    CHECK(r < 3);
}

TEST_CASE("a state written as text reads back")
{
    RandomState s = seeded(99);
    (void)next(s);
    const std::string text = to_text(s);
    CHECK(text.size() == 64);
    CHECK(parse_state(text) == s);
    CHECK(
        to_text(RandomState { { 1, 0, 0, 0xffffffffffffffffULL } })
        == "0000000000000001"
           "0000000000000000"
           "0000000000000000"
           "ffffffffffffffff");
    CHECK_FALSE(parse_state("12"));
    CHECK_FALSE(parse_state(std::string(64, '0')));
    CHECK_FALSE(parse_state(std::string(63, '0') + "g"));
    CHECK_FALSE(parse_state("+" + std::string(63, '1')));
}

TEST_CASE("a seed is given in decimal or hex")
{
    CHECK(parse_seed("12345") == 12345u);
    CHECK(parse_seed("0x1f") == 31u);
    CHECK(parse_seed(seed_text(0xdeadbeefULL)) == 0xdeadbeefULL);
    CHECK(seed_text(31) == "0x000000000000001f");
    CHECK_FALSE(parse_seed(""));
    CHECK_FALSE(parse_seed("0x"));
    CHECK_FALSE(parse_seed("-1"));
    CHECK_FALSE(parse_seed("12a"));
    CHECK_FALSE(parse_seed("18446744073709551616")); // 2^64
}
