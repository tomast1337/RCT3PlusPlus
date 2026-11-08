// SPDX-License-Identifier: GPL-3.0-or-later
#include "logging/crash.hpp"

#include "core/types.hpp"
#include "core/version.hpp"
#include "logging/log.hpp"

#include <csignal>
#include <cstdint>
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

        // strsignal and SIGBUS are POSIX only; UCRT has neither.
        const char* signal_name(int signal) // c-api
        {
#ifdef _WIN32
            switch (signal)
            {
                case SIGABRT:
                    return "SIGABRT";
                case SIGSEGV:
                    return "SIGSEGV";
                case SIGFPE:
                    return "SIGFPE";
                case SIGILL:
                    return "SIGILL";
                default:
                    return "unknown";
            }
#else
            return strsignal(signal);
#endif
        }

        void report(int signal) // c-api
        {
            // shortcut: formats and allocates inside the handler, which is not async-signal-safe; it may hang
            // instead of reporting when the heap itself is broken. Upgrade to a pre-allocated writer if it does.
            std::signal(signal, SIG_DFL);
            const std::string stack = std::to_string(std::stacktrace::current(1));
            const std::string_view version = core::version();
            std::fprintf(stderr, "\nrct3++ %.*s: fatal signal %d (%s); stack:\n%s\n", static_cast<int>(version.size()), version.data(), signal, signal_name(signal), stack.c_str()); // c-api
            if (!g_file.empty())
            {
                if (std::FILE* out = std::fopen(g_file.c_str(), "a"))
                {
                    std::fprintf(out, "\nrct3++ %.*s: fatal signal %d (%s); stack:\n%s\n", static_cast<int>(version.size()), version.data(), signal, signal_name(signal), stack.c_str()); // c-api
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
        for (const i32 signal : { SIGABRT, SIGSEGV, SIGFPE, SIGILL })
            std::signal(signal, report);
#ifdef SIGBUS
        std::signal(SIGBUS, report);
#endif
    }

    void check_failed(std::string_view condition, std::string_view message, std::source_location where)
    {
        logger("check").critical("{}:{}: {} failed: {}", where.file_name(), where.line(), condition, message);
        std::abort();
    }

} // namespace rct3::logging
