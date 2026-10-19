// SPDX-License-Identifier: GPL-3.0-or-later
//
// Logging. Messages go to up to three places at once: the console (coloured when it is a terminal), an
// optional log file, and a fixed-size in-memory ring that keeps the most recent entries. The ring is what an
// in-game console or log panel reads: poll `entries_since(last_sequence)` each frame to get only what is new.
//
//   static const auto log = rct3::logging::logger("ovl");
//   log.info("read {} sounds from {}", count, name);
//
// Categories name the subsystem ("ovl", "save", "audio"). Each category gets a stable colour on the console.
// `NO_COLOR` set in the environment turns colour off; `ColorMode` overrides detection.
//
// All functions are safe to call from any thread.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::logging
{

    enum class Level : std::uint8_t
    {
        Debug,
        Info,
        Warning,
        Error,
        Critical
    };

    enum class ColorMode : std::uint8_t
    {
        Auto, // colour only when the stream is a terminal and NO_COLOR is not set
        Always,
        Never,
    };

    struct Entry
    {
        /// Starts at 1 and increases by one per accepted message, so a reader can ask for "everything after N".
        std::uint64_t sequence = 0;
        std::chrono::system_clock::time_point timestamp;
        Level level = Level::Info;
        std::string category;
        std::string message;
    };

    struct Config
    {
        Level minimum_level = Level::Debug;
        /// Entries kept in memory. 0 disables the ring.
        std::size_t memory_capacity = 1000;
        bool console_enabled = true;
        ColorMode color = ColorMode::Auto;
    };

    class Log;

    /// A named handle onto a Log. Cheap to copy; it must not outlive its Log.
    class Logger
    {
    public:
        Logger() = default;

        [[nodiscard]] bool enabled(Level level) const;

        template<typename... Args> void debug(std::format_string<Args...> pattern, Args&&... args) const
        {
            if (enabled(Level::Debug))
                write(Level::Debug, std::format(pattern, std::forward<Args>(args)...));
        }
        template<typename... Args> void info(std::format_string<Args...> pattern, Args&&... args) const
        {
            if (enabled(Level::Info))
                write(Level::Info, std::format(pattern, std::forward<Args>(args)...));
        }
        template<typename... Args> void warning(std::format_string<Args...> pattern, Args&&... args) const
        {
            if (enabled(Level::Warning))
                write(Level::Warning, std::format(pattern, std::forward<Args>(args)...));
        }
        template<typename... Args> void error(std::format_string<Args...> pattern, Args&&... args) const
        {
            if (enabled(Level::Error))
                write(Level::Error, std::format(pattern, std::forward<Args>(args)...));
        }
        template<typename... Args> void critical(std::format_string<Args...> pattern, Args&&... args) const
        {
            if (enabled(Level::Critical))
                write(Level::Critical, std::format(pattern, std::forward<Args>(args)...));
        }

    private:
        friend class Log;
        Logger(Log& log, std::string category)
            : log_(&log)
            , category_(std::move(category))
        {
        }
        void write(Level level, std::string message) const;

        Log* log_ = nullptr;
        std::string category_;
    };

    class Log
    {
    public:
        explicit Log(Config config = { });
        ~Log();

        Log(const Log&) = delete;
        Log& operator=(const Log&) = delete;

        /// Starts writing to a new timestamped file in `directory` (created if missing). Entries logged before
        /// this call are not written to the file. Returns the file's path, or an error text when it cannot be
        /// opened.
        [[nodiscard]] std::expected<std::filesystem::path, std::string> open_file(const std::filesystem::path& directory);

        [[nodiscard]] Logger for_category(std::string category);
        [[nodiscard]] bool enabled(Level level) const;
        void set_minimum_level(Level level);
        void set_console_enabled(bool enabled);
        /// Applies a whole Config. Changing `memory_capacity` empties the ring; sequences keep counting.
        void reconfigure(const Config& config);

        /// The ring contents, oldest first.
        [[nodiscard]] std::vector<Entry> recent_entries() const;
        /// Ring entries whose sequence is greater than `sequence`, oldest first. Pass 0 for everything held.
        [[nodiscard]] std::vector<Entry> entries_since(std::uint64_t sequence) const;
        /// Sequence of the newest entry accepted so far, 0 when none. Not reset by clear().
        [[nodiscard]] std::uint64_t latest_sequence() const;
        /// Empties the ring. The file and console are unaffected.
        void clear();
        /// Writes the ring, oldest first, as plain text.
        [[nodiscard]] std::expected<void, std::string> write_recent(const std::filesystem::path& path) const;

    private:
        friend class Logger;
        void write(Level level, std::string_view category, std::string message);

        mutable std::mutex mutex_;
        Config config_;
        bool color_stdout_ = false;
        bool color_stderr_ = false;
        std::vector<Entry> ring_;   // storage of `memory_capacity` slots once the first entry arrives
        std::size_t ring_head_ = 0; // index of the oldest entry
        std::size_t ring_count_ = 0;
        std::uint64_t next_sequence_ = 1;
        std::ofstream file_;
    };

    /// The process-wide log used by `logger()`. Created on first use with the default Config.
    [[nodiscard]] Log& global();

    /// Replaces the global log's settings. Call early, before other threads log.
    void configure(const Config& config);

    /// A Logger for `category` on the global log.
    [[nodiscard]] inline Logger logger(std::string category)
    {
        return global().for_category(std::move(category));
    }

    /// Three-letter tag: DBG, INF, WRN, ERR, CRT.
    [[nodiscard]] std::string_view level_tag(Level level);

    /// One line, no trailing newline: `2000-01-31 12:00:00.123 [INF] category: message`. With `color` the line
    /// carries ANSI escape sequences: dim timestamp, level colour, per-category colour.
    [[nodiscard]] std::string format_entry(const Entry& entry, bool color = false);

} // namespace rct3::logging
