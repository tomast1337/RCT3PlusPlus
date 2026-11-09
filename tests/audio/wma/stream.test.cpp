// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/wma/stream.hpp"

#include "../sine_wma.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace rct3::audio::wma;
namespace fs = std::filesystem;

namespace
{
    fs::path write_sine(const char* name, std::size_t size = rct3::test::kSineWma.size())
    {
        const fs::path path = fs::temp_directory_path() / name;
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(rct3::test::kSineWma.data()), static_cast<std::streamsize>(size));
        return path;
    }

    std::vector<float> read_all(WmaStream& s)
    {
        std::vector<float> all;
        std::vector<float> chunk(1000 * s.channels());
        for (;;)
        {
            const auto n = s.read(chunk);
            REQUIRE(n);
            if (*n == 0)
                return all;
            all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(*n * s.channels()));
        }
    }
} // namespace

TEST_CASE("a WMA file decodes to its sound")
{
    const fs::path path = write_sine("rct3pp_sine.wma");
    auto s = WmaStream::open(path);
    REQUIRE(s);
    CHECK(s->sample_rate() == 22050);
    CHECK(s->channels() == 2);
    const auto all = read_all(*s);
    const std::size_t frames = all.size() / 2;
    CHECK(frames >= 22050 / 5); // The encoder leaves out the last part frame
    CHECK(frames <= 22050 * 3 / 10);

    // A 1000 Hz sine: two zero crossings a period, in both channels alike, at the encoder's level.
    std::size_t crossings = 0;
    float peak = 0;
    const std::size_t from = 1000;
    const std::size_t to = 5000;
    for (std::size_t i = from; i < to; ++i)
    {
        crossings += (all[2 * i] < 0) != (all[2 * i + 2] < 0) ? 1 : 0;
        peak = std::max(peak, std::abs(all[2 * i]));
        CHECK(all[2 * i] == doctest::Approx(all[2 * i + 1]).epsilon(0.05));
    }
    const double expected = 2.0 * 1000 * static_cast<double>(to - from) / 22050;
    CHECK(static_cast<double>(crossings) == doctest::Approx(expected).epsilon(0.02));
    CHECK(peak > 0.05f);
    CHECK(peak < 0.2f);

    REQUIRE(s->rewind());
    CHECK(read_all(*s) == all);
    fs::remove(path);
}

TEST_CASE("a cut short WMA file plays what it has")
{
    const fs::path path = write_sine("rct3pp_sine_cut.wma", rct3::test::kSineWma.size() - 600);
    auto s = WmaStream::open(path);
    REQUIRE(s);
    const auto all = read_all(*s);
    CHECK_FALSE(all.empty());
    CHECK(all.size() / 2 < 22050 / 4);
    fs::remove(path);
}

TEST_CASE("files that are not WMA are refused")
{
    CHECK_FALSE(WmaStream::open(fs::temp_directory_path() / "rct3pp_no_such_file.wma"));
    const fs::path path = write_sine("rct3pp_not_wma.wma", 100);
    CHECK_FALSE(WmaStream::open(path));
    fs::remove(path);
}
