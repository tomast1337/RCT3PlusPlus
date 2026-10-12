// SPDX-License-Identifier: GPL-3.0-or-later
//
// nob.hpp: a tiny build helper, C++ flavoured take on the "no build" idea (build scripts written in the
// project's own language, bootstrapped with a single compiler invocation). POSIX only for now.
#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace nob
{

    namespace fs = std::filesystem;

    enum class Level
    {
        Info,
        Warn,
        Error
    };

    template<typename... Args> void log(Level level, std::format_string<Args...> fmt, Args&&... args)
    {
        const char* tag = level == Level::Info ? "INFO" : level == Level::Warn ? "WARN" : "ERROR";
        std::fprintf(stderr, "[%s] %s\n", tag, std::format(fmt, std::forward<Args>(args)...).c_str());
    }

    /// An argv under construction. Paths are added with `.string()` so overloads stay unambiguous.
    struct Cmd
    {
        std::vector<std::string> args;

        Cmd& operator<<(std::string_view arg)
        {
            args.emplace_back(arg);
            return *this;
        }
        Cmd& append(const std::vector<std::string>& more)
        {
            args.insert(args.end(), more.begin(), more.end());
            return *this;
        }

        [[nodiscard]] std::string render() const
        {
            std::string out;
            for (const auto& a : args)
            {
                if (!out.empty())
                    out += ' ';
                out += a;
            }
            return out;
        }
    };

    using Proc = pid_t;

    [[nodiscard]] inline std::optional<Proc> start(const Cmd& cmd)
    {
        if (cmd.args.empty())
            return std::nullopt;
        log(Level::Info, "CMD: {}", cmd.render());

        std::vector<char*> argv;
        argv.reserve(cmd.args.size() + 1);
        for (const auto& a : cmd.args)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        pid_t pid = 0;
        if (int rc = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ); rc != 0)
        {
            log(Level::Error, "cannot start {}: {}", cmd.args[0], std::strerror(rc));
            return std::nullopt;
        }
        return pid;
    }

    [[nodiscard]] inline bool wait(Proc pid)
    {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0)
        {
            if (errno != EINTR)
            {
                log(Level::Error, "waitpid failed: {}", std::strerror(errno));
                return false;
            }
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            return true;
        if (WIFEXITED(status))
            log(Level::Error, "process exited with code {}", WEXITSTATUS(status));
        else
            log(Level::Error, "process terminated by signal");
        return false;
    }

    [[nodiscard]] inline bool run(const Cmd& cmd)
    {
        auto pid = start(cmd);
        return pid && wait(*pid);
    }

    /// Processes running in parallel. `wait_all` always reaps every one, so a failure never leaves zombies.
    class Procs
    {
    public:
        void add(Proc pid)
        {
            pids_.push_back(pid);
        }

        [[nodiscard]] bool wait_all()
        {
            bool ok = true;
            for (Proc pid : pids_)
                ok = wait(pid) && ok;
            pids_.clear();
            return ok;
        }

    private:
        std::vector<Proc> pids_;
    };

    /// True when `output` is missing or older than any input. A missing input counts as "rebuild".
    [[nodiscard]] inline bool needs_rebuild(const fs::path& output, std::span<const fs::path> inputs)
    {
        std::error_code ec;
        const auto out_time = fs::last_write_time(output, ec);
        if (ec)
            return true;
        for (const auto& in : inputs)
        {
            const auto in_time = fs::last_write_time(in, ec);
            if (ec)
            {
                log(Level::Warn, "cannot stat {}: {}", in.string(), ec.message());
                return true;
            }
            if (in_time > out_time)
                return true;
        }
        return false;
    }

    [[nodiscard]] inline bool mkdir_p(const fs::path& dir)
    {
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
        {
            log(Level::Error, "cannot create {}: {}", dir.string(), ec.message());
            return false;
        }
        return true;
    }

    /// Recursively lists files under `dir` with extension `ext` (including the dot), sorted for stable output.
    [[nodiscard]] inline std::vector<fs::path> find_files(const fs::path& dir, std::string_view ext)
    {
        std::vector<fs::path> out;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->is_regular_file() && it->path().extension() == ext)
                out.push_back(it->path());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    /// Rebuilds the build script when its source is newer than the running binary, then re-executes it.
    /// Call first thing in main(). Needs to be started via a path (`./nob`), since it uses argv[0].
    inline void go_rebuild_urself(char** argv, const fs::path& source, std::span<const fs::path> extra_deps = { })
    {
        const fs::path self = argv[0];

        std::vector<fs::path> deps { source };
        deps.insert(deps.end(), extra_deps.begin(), extra_deps.end());
        if (!needs_rebuild(self, deps))
            return;

        log(Level::Info, "rebuilding {}", self.string());
        fs::path old = self;
        old += ".old";
        std::error_code ec;
        fs::rename(self, old, ec);
        if (ec)
        {
            log(Level::Error, "cannot move {} aside: {}", self.string(), ec.message());
            std::exit(1);
        }

        const char* cxx = std::getenv("CXX");
        Cmd cmd;
        cmd << (cxx ? cxx : "g++") << "-std=c++26" << "-o" << self.string() << source.string();
        if (!run(cmd))
        {
            fs::rename(old, self, ec);
            std::exit(1);
        }
        fs::remove(old, ec);

        execv(self.c_str(), argv);
        log(Level::Error, "cannot re-run {}: {}", self.string(), std::strerror(errno));
        std::exit(1);
    }

} // namespace nob
