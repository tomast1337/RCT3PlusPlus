// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/save_text.hpp"

#include "strings/unicode.hpp"

#include <algorithm>
#include <format>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kYear = "Year";
        constexpr std::string_view kShortMonth = "ShortMonth"; ///< Followed by the month's number from 0
        /// The guests line by count; more than four take the last.
        constexpr std::array<std::string_view, 6> kGuests = { "ZeroGuests", "OneGuest", "TwoGuests", "ThreeGuests", "FourGuests", "ManyGuests" };
        constexpr std::int32_t kMinutesPerDay = 24 * 60;

        std::u16string ascii(const std::string& s)
        {
            return std::u16string(s.begin(), s.end());
        }

    } // namespace

    std::u16string clock_text(std::int32_t minutes, bool twelve_hour)
    {
        const std::int32_t m = ((minutes % kMinutesPerDay) + kMinutesPerDay) % kMinutesPerDay;
        const std::int32_t hour = m / 60;
        if (!twelve_hour)
            return ascii(std::format("{}:{:02}", hour, m % 60));
        return ascii(std::format("{}:{:02} {}m", (hour + 11) % 12 + 1, m % 60, hour < 12 ? 'a' : 'p'));
    }

    std::u16string short_month(const StringTable& strings, std::int32_t month)
    {
        return std::u16string(strings.get(std::format("{}{}", kShortMonth, month)));
    }

    std::u16string date_text(const StringTable& strings, std::int32_t day, std::int32_t month, std::int32_t year)
    {
        std::u16string out;
        if (day > 0)
            out = ascii(std::format("{} ", day));
        out += short_month(strings, month);
        out += u", ";
        out += strings.get(kYear);
        out += ascii(std::format(" {}", year));
        return out;
    }

    std::u16string guests_text(const StringTable& strings, std::int32_t guests)
    {
        const auto which = static_cast<std::size_t>(std::clamp(guests, 0, static_cast<std::int32_t>(kGuests.size() - 1)));
        std::u16string out;
        const std::u16string number = ascii(std::to_string(guests));
        for (const char16_t c : strings.get(kGuests.at(which)))
        {
            if (c == u'#')
                out += number;
            else
                out += c;
        }
        return out;
    }

    std::array<std::u16string, 3> save_lines(const StringTable& strings, const save::SaveSummary& summary, bool twelve_hour)
    {
        std::array<std::u16string, 3> lines;
        lines[0] = strings::to_utf16(summary.park_name);
        if (summary.kind != 0)
        {
            // Other summaries keep two sizes where a park keeps its month and year.
            lines[2] = ascii(std::format("{} x {}", summary.month, summary.year));
            return lines;
        }
        lines[1] = date_text(strings, summary.day, summary.month, summary.year) + u"  " + clock_text(summary.minutes, twelve_hour);
        lines[2] = guests_text(strings, summary.guests);
        return lines;
    }

} // namespace rct3::ui
