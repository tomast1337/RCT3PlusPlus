// SPDX-License-Identifier: GPL-3.0-or-later
//
// The three lines a list of saves shows for each one, written as the game writes them from a save's summary
// (see save/summary.hpp): the park's name; the date, two spaces and the time; the number of guests.
//
// The date is "<day> <month>, Year <n>", the day left out when the save has none, with the month's short
// name. The time is "9:15 am" style in English and "21:15" in other languages. The guests line takes its
// wording from a string for the count (no guests, one, two, three, four, more), with '#' for the number.
// Summaries of things other than parks show their two sizes instead, "W x H", and no date.
#pragma once

#include "save/summary.hpp"
#include "ui/string_table.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace rct3::ui
{

    [[nodiscard]] std::u16string clock_text(std::int32_t minutes, bool twelve_hour);
    [[nodiscard]] std::u16string date_text(const StringTable& strings, std::int32_t day, std::int32_t month, std::int32_t year);
    [[nodiscard]] std::u16string guests_text(const StringTable& strings, std::int32_t guests);

    [[nodiscard]] std::array<std::u16string, 3> save_lines(const StringTable& strings, const save::SaveSummary& summary, bool twelve_hour);

} // namespace rct3::ui
