// SPDX-License-Identifier: GPL-3.0-or-later
#include "profiling/profiler.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <stdexcept>
#include <string>
#include <thread>

namespace prof = rct3::profiling;

namespace
{

    struct Fresh
    {
        Fresh()
        {
            prof::set_enabled(true);
            prof::reset();
            prof::register_thread("Test");
        }
        ~Fresh()
        {
            prof::unregister_thread();
        }
    };

    const prof::ScopeStats* find(const std::vector<prof::ScopeStats>& all, const std::string& name)
    {
        const auto it = std::ranges::find(all, name, &prof::ScopeStats::name);
        return it == all.end() ? nullptr : &*it;
    }

} // namespace

TEST_CASE("profiler: nested lambda sections build slash separated paths")
{
    Fresh fresh;
    const int value = prof::section("Render", [] { return prof::section("Entities", [] { return 42; }); });
    CHECK(value == 42);

    const auto all = prof::stats();
    REQUIRE(all.size() == 2);
    CHECK(find(all, "[Test] Render") != nullptr);
    const auto* entities = find(all, "[Test] Render/Entities");
    REQUIRE(entities != nullptr);
    CHECK(entities->last >= 0.0);
}

TEST_CASE("profiler: scope ends when the body throws")
{
    Fresh fresh;
    CHECK_THROWS_AS(prof::section("Load", [] { throw std::runtime_error("x"); }), std::runtime_error);
    // The stack unwound, so the next section is a root again.
    prof::section("After", [] { });
    const auto all = prof::stats();
    CHECK(find(all, "[Test] Load") != nullptr);
    CHECK(find(all, "[Test] After") != nullptr);
    CHECK(find(all, "[Test] Load/After") == nullptr);
}

TEST_CASE("profiler: unregistered thread and disabled profiler record nothing")
{
    Fresh fresh;
    prof::set_enabled(false);
    prof::section("Off", [] { });
    prof::record("Off", 1.0);
    CHECK(prof::stats().empty());
    prof::set_enabled(true);

    std::thread other([] { prof::section("Other", [] { }); });
    other.join();
    CHECK(prof::stats().empty());
}

TEST_CASE("profiler: record, history and percentiles")
{
    Fresh fresh;
    for (int i = 1; i <= 100; ++i)
    {
        prof::record("Frame", static_cast<double>(i));
        prof::capture_frame();
    }
    const auto all = prof::stats();
    const auto* frame = find(all, "[Test] Frame");
    REQUIRE(frame != nullptr);
    CHECK(frame->samples == 100);
    CHECK(frame->last == 100.0);
    CHECK(frame->p50 == 50.0);
    CHECK(frame->p95 == 95.0);
    CHECK(frame->period_max == 100.0);
    REQUIRE(frame->history.size() == 100);
    CHECK(frame->history.front() == 1.0);
    CHECK(frame->history.back() == 100.0);
}

TEST_CASE("profiler: history is a ring of the latest samples")
{
    Fresh fresh;
    for (std::size_t i = 0; i < prof::kHistoryLength + 10; ++i)
    {
        prof::record("Ring", static_cast<double>(i));
        prof::capture_frame();
    }
    const auto all = prof::stats();
    const auto* ring = find(all, "[Test] Ring");
    REQUIRE(ring != nullptr);
    REQUIRE(ring->history.size() == prof::kHistoryLength);
    CHECK(ring->history.front() == 10.0);
}

TEST_CASE("profiler: period maximum rolls after one second")
{
    Fresh fresh;
    prof::record("Spike", 9.0);
    prof::update(0.5);
    prof::record("Spike", 1.0);
    prof::update(0.6); // rolls: previous = 9, current = 0
    prof::record("Spike", 2.0);
    const auto all = prof::stats();
    const auto* spike = find(all, "[Test] Spike");
    REQUIRE(spike != nullptr);
    CHECK(spike->period_max == 9.0);
    prof::update(1.0); // rolls again: previous = 2
    const auto again = prof::stats();
    CHECK(find(again, "[Test] Spike")->period_max == 2.0);
}

TEST_CASE("profiler: tsv snapshot has a header and one row per scope")
{
    Fresh fresh;
    prof::record("A", 1.5);
    prof::capture_frame();
    const std::string text = prof::tsv_snapshot();
    CHECK(text.starts_with("scope\tsamples\tlastMs\taverageMs\tp50Ms\tp95Ms\tperiodMaxMs\n"));
    CHECK(text.find("[Test] A\t1\t1.500000\t1.500000\t1.500000\t1.500000\t1.500000\n") != std::string::npos);
}
