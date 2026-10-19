// SPDX-License-Identifier: GPL-3.0-or-later
//
// A report when the program dies of a fatal signal (a failed assertion's abort, a bad memory access): which
// signal, then the stack of the thread that died, with source lines where debug information has them,
// printed to the standard error and appended to the run's log file when it has one.
//
// Libraries' own checks (EnTT's, through simulation/entt.hpp) report through check_failed(): what failed is
// logged, then the abort brings the report.
#pragma once

#include <filesystem>
#include <source_location>
#include <string_view>

namespace rct3::logging
{

    /// Sets the report up for the fatal signals. Call once, early in main; again with `file` once the run's
    /// log file is open, to append the report there too.
    void install_crash_report(const std::filesystem::path& file = { });

    /// A failed check: logs `condition` and `message` at `where` as critical, then aborts.
    [[noreturn]] void check_failed(std::string_view condition, std::string_view message, std::source_location where = std::source_location::current());

} // namespace rct3::logging
