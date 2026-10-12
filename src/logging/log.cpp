// SPDX-License-Identifier: GPL-3.0-or-later
#include "logging/log.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#ifdef _WIN32
    #include <io.h>
    #define RCT3_ISATTY(f) _isatty(_fileno(f))
#else
    #include <unistd.h>
    #define RCT3_ISATTY(f) isatty(fileno(f))
#endif

namespace rct3::logging
{
    namespace
    {

        constexpr std::string_view kReset = "\x1b[0m";
        constexpr std::string_view kDim = "\x1b[90m";

        std::string_view level_color(Level level)
        {
            switch (level)
            {
                case Level::Debug:
                    return "\x1b[90m"; // grey
                case Level::Info:
                    return "\x1b[32m"; // green
                case Level::Warning:
                    return "\x1b[33m"; // yellow
                case Level::Error:
                    return "\x1b[31m"; // red
                case Level::Critical:
                    return "\x1b[1;97;41m"; // bold white on red
            }
            return "";
        }

        // A stable colour per category, so a subsystem is easy to follow in a busy log.
        std::string_view category_color(std::string_view category)
        {
            static constexpr std::array<std::string_view, 6> kPalette = {
                "\x1b[36m", "\x1b[35m", "\x1b[34m", "\x1b[96m", "\x1b[95m", "\x1b[94m",
            };
            std::uint32_t h = 2166136261u; // FNV-1a
            for (const char c : category)
                h = (h ^ static_cast<std::uint8_t>(c)) * 16777619u;
            return kPalette[h % kPalette.size()];
        }

        std::tm local_time(std::time_t t)
        {
            std::tm out { };
#ifdef _WIN32
            localtime_s(&out, &t);
#else
            localtime_r(&t, &out);
#endif
            return out;
        }

        std::string timestamp_text(std::chrono::system_clock::time_point tp)
        {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
            const std::tm tm = local_time(std::chrono::system_clock::to_time_t(tp));
            char buf[32];
            std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
            return std::format("{}.{:03}", buf, static_cast<int>(((ms.count() % 1000) + 1000) % 1000));
        }

        bool use_color(ColorMode mode, std::FILE* stream)
        {
            switch (mode)
            {
                case ColorMode::Always:
                    return true;
                case ColorMode::Never:
                    return false;
                case ColorMode::Auto:
                    break;
            }
            const char* no_color = std::getenv("NO_COLOR");
            if (no_color != nullptr && *no_color != '\0')
                return false;
            return RCT3_ISATTY(stream) != 0;
        }

    } // namespace

    std::string_view level_tag(Level level)
    {
        switch (level)
        {
            case Level::Debug:
                return "DBG";
            case Level::Info:
                return "INF";
            case Level::Warning:
                return "WRN";
            case Level::Error:
                return "ERR";
            case Level::Critical:
                return "CRT";
        }
        return "???";
    }

    std::string format_entry(const Entry& entry, bool color)
    {
        const std::string time = timestamp_text(entry.timestamp);
        if (!color)
            return std::format("{} [{}] {}: {}", time, level_tag(entry.level), entry.category, entry.message);
        return std::format("{}{}{} {}[{}]{} {}{}{}: {}", kDim, time, kReset, level_color(entry.level), level_tag(entry.level), kReset, category_color(entry.category), entry.category, kReset, entry.message);
    }

    bool Logger::enabled(Level level) const
    {
        return log_ != nullptr && log_->enabled(level);
    }

    void Logger::write(Level level, std::string message) const
    {
        if (log_ != nullptr)
            log_->write(level, category_, std::move(message));
    }

    Log::Log(Config config)
        : config_(config)
    {
        color_stdout_ = use_color(config_.color, stdout);
        color_stderr_ = use_color(config_.color, stderr);
    }

    Log::~Log()
    {
        const std::scoped_lock lock(mutex_);
        if (file_)
            file_.flush();
    }

    std::expected<void, std::string> Log::open_file(const std::filesystem::path& directory)
    {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec)
            return std::unexpected(std::format("cannot create {}: {}", directory.string(), ec.message()));
        const std::tm tm = local_time(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
        char name[40];
        std::strftime(name, sizeof name, "%Y-%m-%d_%H.%M.%S.log", &tm);
        const auto path = directory / name;
        const std::scoped_lock lock(mutex_);
        file_.close();
        file_.clear();
        file_.open(path, std::ios::out | std::ios::app);
        if (!file_)
            return std::unexpected(std::format("cannot open log file {}", path.string()));
        return { };
    }

    Logger Log::for_category(std::string category)
    {
        return Logger(*this, std::move(category));
    }

    bool Log::enabled(Level level) const
    {
        const std::scoped_lock lock(mutex_);
        return level >= config_.minimum_level;
    }

    void Log::set_minimum_level(Level level)
    {
        const std::scoped_lock lock(mutex_);
        config_.minimum_level = level;
    }

    void Log::set_console_enabled(bool enabled)
    {
        const std::scoped_lock lock(mutex_);
        config_.console_enabled = enabled;
    }

    void Log::reconfigure(const Config& config)
    {
        const std::scoped_lock lock(mutex_);
        if (config.memory_capacity != config_.memory_capacity)
        {
            ring_.clear();
            ring_head_ = 0;
            ring_count_ = 0;
        }
        config_ = config;
        color_stdout_ = use_color(config_.color, stdout);
        color_stderr_ = use_color(config_.color, stderr);
    }

    std::vector<Entry> Log::recent_entries() const
    {
        return entries_since(0);
    }

    std::vector<Entry> Log::entries_since(std::uint64_t sequence) const
    {
        const std::scoped_lock lock(mutex_);
        std::vector<Entry> out;
        out.reserve(ring_count_);
        for (std::size_t i = 0; i < ring_count_; ++i)
        {
            const Entry& e = ring_[(ring_head_ + i) % ring_.size()];
            if (e.sequence > sequence)
                out.push_back(e);
        }
        return out;
    }

    std::uint64_t Log::latest_sequence() const
    {
        const std::scoped_lock lock(mutex_);
        return next_sequence_ - 1;
    }

    void Log::clear()
    {
        const std::scoped_lock lock(mutex_);
        ring_head_ = 0;
        ring_count_ = 0;
    }

    std::expected<void, std::string> Log::write_recent(const std::filesystem::path& path) const
    {
        const auto entries = recent_entries();
        if (!path.parent_path().empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
                return std::unexpected(std::format("cannot create {}: {}", path.parent_path().string(), ec.message()));
        }
        std::ofstream out(path);
        if (!out)
            return std::unexpected(std::format("cannot write {}", path.string()));
        for (const auto& e : entries)
            out << format_entry(e) << '\n';
        if (!out)
            return std::unexpected(std::format("write failed for {}", path.string()));
        return { };
    }

    void Log::write(Level level, std::string_view category, std::string message)
    {
        Entry entry;
        entry.timestamp = std::chrono::system_clock::now();
        entry.level = level;
        entry.category = std::string(category);
        entry.message = std::move(message);

        const std::scoped_lock lock(mutex_);
        if (level < config_.minimum_level)
            return;
        entry.sequence = next_sequence_++;

        if (config_.console_enabled)
        {
            const bool to_err = level >= Level::Warning;
            std::FILE* stream = to_err ? stderr : stdout;
            const std::string line = format_entry(entry, to_err ? color_stderr_ : color_stdout_);
            std::fputs(line.c_str(), stream);
            std::fputc('\n', stream);
            std::fflush(stream);
        }
        if (file_)
        {
            file_ << format_entry(entry) << '\n';
            file_.flush();
        }
        if (config_.memory_capacity > 0)
        {
            if (ring_.size() != config_.memory_capacity)
            {
                ring_.assign(config_.memory_capacity, Entry { });
                ring_head_ = 0;
                ring_count_ = 0;
            }
            if (ring_count_ < ring_.size())
            {
                ring_[(ring_head_ + ring_count_) % ring_.size()] = std::move(entry);
                ++ring_count_;
            }
            else
            {
                ring_[ring_head_] = std::move(entry);
                ring_head_ = (ring_head_ + 1) % ring_.size();
            }
        }
    }

    Log& global()
    {
        static Log instance;
        return instance;
    }

    void configure(const Config& config)
    {
        global().reconfigure(config);
    }

} // namespace rct3::logging
