// SPDX-License-Identifier: GPL-3.0-or-later
#include "logging/crash.hpp"

#include "logging/log.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstring> // sigabbrev_np
#include <stacktrace>
#include <string>

namespace rct3::logging
{

    namespace
    {
        std::string g_file; ///< Where the report is appended too; empty for none

        void report(int signal)
        {
            // shortcut: formats and allocates inside the handler, which is not async-signal-safe; it may hang
            // instead of reporting when the heap itself is broken. Upgrade to a pre-allocated writer if it does.
            std::signal(signal, SIG_DFL);
            const std::string stack = std::to_string(std::stacktrace::current(1));
            std::fprintf(stderr, "\nfatal signal %d (%s); stack:\n%s\n", signal, strsignal(signal), stack.c_str());
            if (!g_file.empty())
            {
                if (std::FILE* out = std::fopen(g_file.c_str(), "a"))
                {
                    std::fprintf(out, "\nfatal signal %d (%s); stack:\n%s\n", signal, strsignal(signal), stack.c_str());
                    std::fclose(out);
                }
                std::fprintf(stderr, "report appended to %s\n", g_file.c_str());
            }
            std::fflush(stderr);
            std::raise(signal);
        }
    } // namespace

    void install_crash_report(const std::filesystem::path& file)
    {
        g_file = file.string();
        for (const int signal : { SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL })
            std::signal(signal, report);
    }

    void check_failed(std::string_view condition, std::string_view message, std::source_location where)
    {
        logger("check").critical("{}:{}: {} failed: {}", where.file_name(), where.line(), condition, message);
        std::abort();
    }

} // namespace rct3::logging
