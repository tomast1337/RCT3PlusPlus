// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small hierarchical profiler. Sections are timed with RAII scopes or by wrapping code in a lambda; nesting
// builds "/"-separated paths. Each registered thread (for example "Main", "Worker") has its own scope tree.
//
// Scope names are PascalCase with no separators; hierarchy comes from nesting only:
//
//   profiling::section("Render", [&] {
//     profiling::section("Entities", [&] { draw_entities(); });   // recorded as "Render/Entities"
//   });
//
// Calls from a thread that has not been registered, or while the profiler is disabled, cost one branch and
// record nothing.
#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace rct3::profiling {

/// Samples kept per scope for percentile and graph use.
inline constexpr std::size_t kHistoryLength = 300;

namespace detail {
struct Context;
}

/// Makes the calling thread record into the context called `name`. Registering the same name again
/// reuses that context. Call once at the start of each thread that should be profiled.
void register_thread(std::string_view name);

/// Stops the calling thread from recording. Its collected data stays available.
void unregister_thread();

/// Global switch. Disabled profilers record nothing. Enabled by default.
void set_enabled(bool enabled);
[[nodiscard]] bool enabled();

/// Drops all collected data in every context. Thread registrations are kept.
void reset();

/// Records a value measured elsewhere, in milliseconds, under `name` at the current nesting level.
void record(std::string_view name, double milliseconds);

/// Advances the one-second timer that rolls the period maximum. Call once per frame.
void update(double delta_seconds);

/// Stores each scope's latest value in its history. Call once per frame.
void capture_frame();

/// Times the enclosing block. Prefer `section` when the timed code is a self-contained piece of work.
class Scope {
 public:
  explicit Scope(std::string_view name);
  ~Scope();

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  detail::Context* context_ = nullptr;
  std::string path_;
  std::chrono::steady_clock::time_point start_{};
};

/// Runs `body` inside a scope called `name` and returns its result. The scope ends on return or exception.
template <typename F>
decltype(auto) section(std::string_view name, F&& body) {
  Scope scope(name);
  return std::forward<F>(body)();
}

/// Statistics of one scope. Times are milliseconds.
struct ScopeStats {
  std::string name;    ///< "[Thread] Parent/Child"
  std::size_t samples = 0;
  double last = 0;
  double average = 0;  ///< Exponential moving average
  double p50 = 0;
  double p95 = 0;
  double period_max = 0;  ///< Maximum over the current and previous one-second periods
  std::vector<double> history;  ///< Oldest first, at most kHistoryLength values
};

/// Statistics of every scope of every context, sorted by name.
[[nodiscard]] std::vector<ScopeStats> stats();

/// Tab-separated dump of `stats()` with a header row, for logs and automated diagnostics.
[[nodiscard]] std::string tsv_snapshot();

}  // namespace rct3::profiling
