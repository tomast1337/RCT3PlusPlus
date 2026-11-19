// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/id.hpp"

#include "core/types.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <type_traits>
#include <unordered_set>

namespace
{
    using AppleIndex = rct3::core::Id<struct AppleTag>;
    using PearIndex = rct3::core::Id<struct PearTag>;
} // namespace

TEST_CASE("an id is made from a number only by naming its type, and is no other kind of id")
{
    static_assert(!std::is_convertible_v<usize, AppleIndex>);
    static_assert(!std::is_convertible_v<i32, AppleIndex>);
    static_assert(!std::is_convertible_v<AppleIndex, usize>);
    static_assert(!std::is_convertible_v<AppleIndex, PearIndex>);
    static_assert(!std::is_constructible_v<PearIndex, AppleIndex>);
    static_assert(sizeof(AppleIndex) == sizeof(usize));

    constexpr AppleIndex third { 2 };
    CHECK(third.value == 2);
    CHECK(AppleIndex { }.value == 0);
}

TEST_CASE("ids of one kind compare and hash as their numbers")
{
    CHECK(AppleIndex { 1 } == AppleIndex { 1 });
    CHECK(AppleIndex { 1 } != AppleIndex { 2 });
    CHECK(AppleIndex { 1 } < AppleIndex { 2 });

    const std::unordered_set<AppleIndex> seen { AppleIndex { 3 }, AppleIndex { 3 }, AppleIndex { 4 } };
    CHECK(seen.size() == 2);
    CHECK(seen.contains(AppleIndex { 4 }));
}
