// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The awards a park can be given, and the fines: each has a number, as the park file keeps them, a name and
// whether it is a fine.

#include <cstdint>
#include <string_view>

namespace rct3::simulation
{

    /// Kinds of award, numbered as saved.
    inline constexpr std::int32_t kAwardKinds = 31;

    /// Text key of award `kind`'s name; empty for a number that is no award.
    [[nodiscard]] std::string_view award_name(std::int32_t kind);

    /// Whether award `kind` is a fine, which costs the park rather than paying it.
    [[nodiscard]] bool award_is_fine(std::int32_t kind);

    /// Text key of the message telling the player they have award `kind`.
    [[nodiscard]] std::string_view award_message(std::int32_t kind);

} // namespace rct3::simulation
