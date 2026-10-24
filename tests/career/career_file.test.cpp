// SPDX-License-Identifier: GPL-3.0-or-later
#include "career/career_file.hpp"

#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <format>
#include <unistd.h>

using namespace rct3;
using namespace rct3::career;
namespace fs = std::filesystem;

namespace
{
    // A scratch folder removed on scope exit.
    struct TempFolder
    {
        fs::path path = fs::temp_directory_path() / std::format("rct3pp_career_test_{}", ::getpid());
        TempFolder()
        {
            fs::create_directories(path);
        }
        ~TempFolder()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

    CareerData sample()
    {
        CareerData d;
        d.career = new_career("Someone", 2, std::vector<std::string> { "Hill", "Lake", "Mine", "Dunes" });
        d.career.created = 1'700'000'000;
        d.career.current = 1;
        ScenarioProgress& p = d.career.scenarios[1];
        complete(d.career, 1, ScenarioStatus::Entrepreneur, 1'700'000'500);
        p.last_save = "Scenario02/Sunny days.dat";
        p.play_seconds = 4000;
        d.career.unlisted.push_back({ .id = "Gone", .status = ScenarioStatus::Tycoon });
        return d;
    }

    std::size_t find(const std::vector<std::byte>& bytes, std::string_view tag)
    {
        for (std::size_t i = 0; i + tag.size() <= bytes.size(); ++i)
        {
            if (std::memcmp(bytes.data() + i, tag.data(), tag.size()) == 0)
                return i;
        }
        return bytes.size();
    }
} // namespace

TEST_CASE("a career reads back as written")
{
    const auto bytes = write_career(sample());
    const auto back = read_career(bytes);
    REQUIRE(back);
    const Career& c = back->career;
    CHECK(c.name == "Someone");
    CHECK(c.pack == 2);
    CHECK(c.created == 1'700'000'000);
    REQUIRE(c.scenarios.size() == 5); // The unlisted one comes back with the rest, for fit() to sort
    CHECK(c.current == 1);
    CHECK(c.scenarios[1].id == "Lake");
    CHECK(c.scenarios[1].status == ScenarioStatus::Entrepreneur);
    CHECK(c.scenarios[1].last_save == "Scenario02/Sunny days.dat");
    CHECK(c.scenarios[1].reached[1] == 1'700'000'500);
    CHECK(c.scenarios[1].play_seconds == 4000);
    CHECK(c.scenarios[4].id == "Gone");
}

TEST_CASE("chunks this version does not read are kept, and longer chunks read")
{
    CareerData d = sample();
    d.unknown.push_back({ { 'N', 'O', 'T', 'E' }, { std::byte { 1 }, std::byte { 2 } } });
    const auto back = read_career(write_career(d));
    REQUIRE(back);
    REQUIRE(back->unknown.size() == 1);
    CHECK(back->unknown[0].tag == std::array<char, 4> { 'N', 'O', 'T', 'E' });
    CHECK(back->unknown[0].payload.size() == 2);
    CHECK(write_career(*back) == write_career(d));
}

TEST_CASE("files that do not check out are not read")
{
    auto bytes = write_career(sample());
    CHECK(read_career(std::span(bytes).first(3)).error() == CareerFileError::NotACareer);

    auto other = bytes;
    other[0] = std::byte { 'X' };
    CHECK(read_career(other).error() == CareerFileError::NotACareer);

    auto newer = bytes;
    newer[4] = std::byte { 9 };
    CHECK(read_career(newer).error() == CareerFileError::NewerVersion);

    auto damaged = bytes;
    damaged[find(damaged, "Someone")] = std::byte { 's' };
    CHECK(read_career(damaged).error() == CareerFileError::BadChecksum);

    const std::size_t end = find(bytes, "END ");
    CHECK(read_career(std::span(bytes).first(end)).error() == CareerFileError::Truncated);
    CHECK(read_career(std::span(bytes).first(bytes.size() - 1)).error() == CareerFileError::Truncated);
}

TEST_CASE("games are known by their ids")
{
    CHECK(pack_id(0) == "base");
    CHECK(pack_id(2) == "wild");
    CHECK(pack_id(7).empty());
    CHECK(pack_from_id("soaked") == 1);
    CHECK_FALSE(pack_from_id("other"));
}

TEST_CASE("careers are listed by game, numbered from the first free name and deleted with their games")
{
    TempFolder folder;
    const fs::path first = free_career_path(folder.path);
    CHECK(first.filename() == "career_0000.rct3career");
    CareerData one;
    one.career = new_career("One", 0, std::vector<std::string> { "A", "B" });
    REQUIRE(save_career(first, one));
    const fs::path second = free_career_path(folder.path);
    CHECK(second.filename() == "career_0001.rct3career");
    CareerData other;
    other.career = new_career("Other game", 1, std::vector<std::string> { "C" });
    REQUIRE(save_career(second, other));
    REQUIRE(config::write_text_file(folder.path / "career_0002.rct3career", "not a career"));
    CareerData two;
    two.career = new_career("Two", 0, std::vector<std::string> { "A" });
    REQUIRE(save_career(free_career_path(folder.path), two));

    std::vector<config::FileProblem> problems;
    const auto careers = list_careers(folder.path, 0, problems);
    REQUIRE(careers.size() == 2);
    CHECK(careers[0].data.career.name == "One");
    CHECK(careers[1].data.career.name == "Two");
    CHECK(problems.size() == 1);

    fs::create_directories(folder.path / "career_0000" / "Hill");
    REQUIRE(delete_career(first));
    CHECK_FALSE(fs::exists(first));
    CHECK_FALSE(fs::exists(folder.path / "career_0000"));
    CHECK(free_career_path(folder.path).filename() == "career_0000.rct3career");
}

TEST_CASE("a career is changed in its file")
{
    TempFolder folder;
    const fs::path path = folder.path / "career_0000.rct3career";
    REQUIRE(save_career(path, sample()));
    REQUIRE(change_career(path, [](Career& c) { complete(c, 0, ScenarioStatus::Apprentice, 1'700'001'000); }));
    const auto careers = list_careers(folder.path, 2, std::nullopt);
    REQUIRE(careers.size() == 1);
    CHECK(careers[0].data.career.scenarios[0].status == ScenarioStatus::Apprentice);
    CHECK(careers[0].data.career.scenarios[1].last_save == "Scenario02/Sunny days.dat");
    CHECK_FALSE(change_career(folder.path / "missing.rct3career", [](Career&) { }));
}
