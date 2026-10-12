// SPDX-License-Identifier: GPL-3.0-or-later
//
// Logging checks. The console is switched off in every test; output goes to the ring or to a temp directory.
#include "logging/log.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace rct3::logging;

namespace
{

    Config quiet(std::size_t capacity = 4)
    {
        Config c;
        c.console_enabled = false;
        c.memory_capacity = capacity;
        return c;
    }

} // namespace

TEST_CASE("messages are formatted and kept with increasing sequence numbers")
{
    Log log(quiet());
    const auto a = log.for_category("alpha");
    a.info("value {} of {}", 3, "four");
    a.warning("second");
    const auto entries = log.recent_entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].sequence == 1);
    CHECK(entries[0].category == "alpha");
    CHECK(entries[0].message == "value 3 of four");
    CHECK(entries[0].level == Level::Info);
    CHECK(entries[1].sequence == 2);
    CHECK(entries[1].level == Level::Warning);
    CHECK(log.latest_sequence() == 2);
}

TEST_CASE("the ring keeps only the newest entries, oldest first")
{
    Log log(quiet(3));
    const auto l = log.for_category("ring");
    for (int i = 1; i <= 7; ++i)
        l.info("m{}", i);
    const auto entries = log.recent_entries();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].message == "m5");
    CHECK(entries[1].message == "m6");
    CHECK(entries[2].message == "m7");
    CHECK(entries[2].sequence == 7);
}

TEST_CASE("entries_since returns only newer entries")
{
    Log log(quiet(10));
    const auto l = log.for_category("poll");
    l.info("a");
    l.info("b");
    const auto cursor = log.latest_sequence();
    CHECK(log.entries_since(cursor).empty());
    l.info("c");
    l.info("d");
    const auto fresh = log.entries_since(cursor);
    REQUIRE(fresh.size() == 2);
    CHECK(fresh[0].message == "c");
    CHECK(fresh[1].message == "d");
    CHECK(log.entries_since(0).size() == 4);
}

TEST_CASE("a reader that falls behind the ring sees what is left")
{
    Log log(quiet(2));
    const auto l = log.for_category("slow");
    l.info("1");
    const auto cursor = log.latest_sequence();
    for (int i = 0; i < 5; ++i)
        l.info("x{}", i);
    const auto fresh = log.entries_since(cursor);
    CHECK(fresh.size() == 2);
    CHECK(fresh.back().message == "x4");
}

TEST_CASE("level filtering drops messages without formatting them")
{
    Config c = quiet(8);
    c.minimum_level = Level::Warning;
    Log log(c);
    const auto l = log.for_category("filter");
    CHECK_FALSE(l.enabled(Level::Info));
    l.debug("no");
    l.info("no");
    l.warning("yes");
    l.error("yes");
    l.critical("yes");
    CHECK(log.recent_entries().size() == 3);
    log.set_minimum_level(Level::Debug);
    l.debug("now");
    CHECK(log.recent_entries().size() == 4);
}

TEST_CASE("clear empties the ring but keeps counting")
{
    Log log(quiet(4));
    const auto l = log.for_category("c");
    l.info("a");
    log.clear();
    CHECK(log.recent_entries().empty());
    l.info("b");
    CHECK(log.recent_entries().front().sequence == 2);
}

TEST_CASE("zero capacity disables the ring")
{
    Log log(quiet(0));
    log.for_category("n").info("x");
    CHECK(log.recent_entries().empty());
    CHECK(log.latest_sequence() == 1);
}

TEST_CASE("reconfigure changes capacity and level")
{
    Log log(quiet(2));
    const auto l = log.for_category("r");
    l.info("a");
    Config c = quiet(5);
    c.minimum_level = Level::Error;
    log.reconfigure(c);
    CHECK(log.recent_entries().empty());
    l.info("hidden");
    l.error("shown");
    CHECK(log.recent_entries().size() == 1);
}

TEST_CASE("formatted lines: plain and coloured")
{
    Entry e;
    e.sequence = 1;
    e.level = Level::Error;
    e.category = "save";
    e.message = "bad header";
    const std::string plain = format_entry(e, false);
    CHECK(plain.find("[ERR] save: bad header") != std::string::npos);
    CHECK(plain.find('\x1b') == std::string::npos);
    const std::string colored = format_entry(e, true);
    CHECK(colored.find('\x1b') != std::string::npos);
    CHECK(colored.find("bad header") != std::string::npos);
    CHECK(colored.find("ERR") != std::string::npos);
    // Same category, same colour: the escape sequence before the category name is stable.
    Entry other = e;
    other.message = "different";
    const auto at = colored.find("save");
    const auto at2 = format_entry(other, true).find("save");
    CHECK(colored.substr(at - 5, 5) == format_entry(other, true).substr(at2 - 5, 5));
    CHECK(level_tag(Level::Debug) == "DBG");
    CHECK(level_tag(Level::Critical) == "CRT");
}

TEST_CASE("each level has a distinct colour")
{
    std::vector<std::string> seen;
    for (const auto level : { Level::Debug, Level::Info, Level::Warning, Level::Error, Level::Critical })
    {
        Entry e;
        e.level = level;
        e.category = "x";
        e.message = "m";
        const auto line = format_entry(e, true);
        const auto start = line.find("\x1b[", line.find("\x1b[0m") + 4); // the level escape follows the timestamp's reset
        seen.push_back(line.substr(start, line.find('m', start) - start));
    }
    std::sort(seen.begin(), seen.end());
    CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
}

TEST_CASE("a log file is written and write_recent dumps the ring")
{
    const auto dir = std::filesystem::temp_directory_path() / "rct3_log_test";
    std::filesystem::remove_all(dir);
    {
        Log log(quiet(8));
        REQUIRE(log.open_file(dir / "logs").has_value());
        log.for_category("file").info("hello {}", 1);
        REQUIRE(log.write_recent(dir / "recent" / "ring.txt").has_value());
    }
    std::size_t files = 0;
    std::string text;
    for (const auto& e : std::filesystem::directory_iterator(dir / "logs"))
    {
        ++files;
        std::ifstream in(e.path());
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(files == 1);
    CHECK(text.find("[INF] file: hello 1") != std::string::npos);
    std::ifstream ring(dir / "recent" / "ring.txt");
    const std::string ring_text((std::istreambuf_iterator<char>(ring)), std::istreambuf_iterator<char>());
    CHECK(ring_text.find("hello 1") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("open_file reports an error when the directory cannot be created")
{
    const auto dir = std::filesystem::temp_directory_path() / "rct3_log_blocker";
    std::filesystem::remove_all(dir);
    {
        std::ofstream(dir) << "a file, not a directory";
    }
    Log log(quiet());
    const auto result = log.open_file(dir / "sub");
    CHECK_FALSE(result.has_value());
    std::filesystem::remove(dir);
}

TEST_CASE("concurrent writers keep sequences unique")
{
    Log log(quiet(400));
    const auto l = log.for_category("mt");
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&l] {
            for (int i = 0; i < 100; ++i)
                l.info("{}", i);
        });
    }
    for (auto& t : threads)
        t.join();
    const auto entries = log.recent_entries();
    REQUIRE(entries.size() == 400);
    for (std::size_t i = 0; i < entries.size(); ++i)
        CHECK(entries[i].sequence == i + 1);
}

TEST_CASE("an unattached logger is harmless")
{
    const Logger none;
    CHECK_FALSE(none.enabled(Level::Critical));
    none.critical("goes nowhere {}", 1);
}

TEST_CASE("the global log is shared")
{
    Config c = quiet(16);
    configure(c);
    global().clear();
    const auto before = global().latest_sequence();
    logger("global").info("seen");
    CHECK(global().latest_sequence() == before + 1);
    CHECK(global().recent_entries().back().message == "seen");
}
