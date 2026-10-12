// SPDX-License-Identifier: GPL-3.0-or-later
#include "profiling/profiler.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace rct3::profiling
{

    namespace detail
    {

        struct ScopeData
        {
            std::array<double, kHistoryLength> history { };
            std::size_t head = 0;
            std::size_t count = 0;
            double last = 0;
            double average = 0;
            double current_max = 0;
            double previous_max = 0;

            void add(double ms)
            {
                last = ms;
                average = average == 0 ? ms : average * 0.95 + ms * 0.05;
                current_max = std::max(current_max, ms);
            }
        };

        struct Context
        {
            explicit Context(std::string n)
                : name(std::move(n))
            {
            }

            const std::string name;
            // Only the owning thread touches the path stack. Everything else is shared with readers.
            std::vector<std::string> path_stack;
            std::mutex gate;
            std::unordered_map<std::string, ScopeData> scopes;
            double period_timer = 0;

            std::string current_path(std::string_view leaf) const
            {
                if (path_stack.empty())
                    return std::string(leaf);
                std::string path = path_stack.back();
                path += '/';
                path += leaf;
                return path;
            }

            void add(const std::string& path, double ms)
            {
                std::lock_guard lock(gate);
                scopes[path].add(ms);
            }
        };

    } // namespace detail

    namespace
    {

        std::atomic<bool> g_enabled { true };
        thread_local detail::Context* t_context = nullptr;

        struct Registry
        {
            std::mutex mutex;
            std::vector<std::unique_ptr<detail::Context>> contexts;
        };

        // Leaked on purpose so threads that outlive static destruction can still record.
        Registry& registry()
        {
            static Registry* instance = new Registry;
            return *instance;
        }

        template<typename F> void for_each_context(F&& fn)
        {
            Registry& r = registry();
            std::lock_guard lock(r.mutex);
            for (auto& c : r.contexts)
                fn(*c);
        }

        double percentile(const std::vector<double>& sorted, double p)
        {
            if (sorted.empty())
                return 0;
            const auto rank = static_cast<std::ptrdiff_t>(std::ceil(static_cast<double>(sorted.size()) * p)) - 1;
            const auto index = std::clamp<std::ptrdiff_t>(rank, 0, static_cast<std::ptrdiff_t>(sorted.size()) - 1);
            return sorted[static_cast<std::size_t>(index)];
        }

        void append_fixed(std::string& out, double value)
        {
            std::array<char, 64> buffer { };
            const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed, 6);
            out.append(buffer.data(), result.ptr);
        }

    } // namespace

    void register_thread(std::string_view name)
    {
        Registry& r = registry();
        std::lock_guard lock(r.mutex);
        for (auto& c : r.contexts)
        {
            if (c->name == name)
            {
                t_context = c.get();
                return;
            }
        }
        r.contexts.push_back(std::make_unique<detail::Context>(std::string(name)));
        t_context = r.contexts.back().get();
    }

    void unregister_thread()
    {
        t_context = nullptr;
    }

    void set_enabled(bool on)
    {
        g_enabled.store(on, std::memory_order_relaxed);
    }
    bool enabled()
    {
        return g_enabled.load(std::memory_order_relaxed);
    }

    void reset()
    {
        for_each_context([](detail::Context& c) {
            std::lock_guard lock(c.gate);
            c.scopes.clear();
            c.period_timer = 0;
        });
    }

    void record(std::string_view name, double milliseconds)
    {
        if (t_context == nullptr || !enabled())
            return;
        t_context->add(t_context->current_path(name), milliseconds);
    }

    void update(double delta_seconds)
    {
        for_each_context([delta_seconds](detail::Context& c) {
            std::lock_guard lock(c.gate);
            c.period_timer += delta_seconds;
            if (c.period_timer < 1.0)
                return;
            c.period_timer = 0;
            for (auto& [path, data] : c.scopes)
            {
                data.previous_max = data.current_max;
                data.current_max = 0;
            }
        });
    }

    void capture_frame()
    {
        for_each_context([](detail::Context& c) {
            std::lock_guard lock(c.gate);
            for (auto& [path, data] : c.scopes)
            {
                data.history[data.head] = data.last;
                data.head = (data.head + 1) % kHistoryLength;
                data.count = std::min(data.count + 1, kHistoryLength);
            }
        });
    }

    Scope::Scope(std::string_view name)
    {
        if (t_context == nullptr || !enabled())
            return;
        context_ = t_context;
        path_ = context_->current_path(name);
        context_->path_stack.push_back(path_);
        start_ = std::chrono::steady_clock::now();
    }

    Scope::~Scope()
    {
        if (context_ == nullptr)
            return;
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
        context_->path_stack.pop_back();
        context_->add(path_, ms);
    }

    std::vector<ScopeStats> stats()
    {
        std::vector<ScopeStats> result;
        for_each_context([&result](detail::Context& c) {
            std::lock_guard lock(c.gate);
            for (const auto& [path, data] : c.scopes)
            {
                ScopeStats s;
                s.name = "[" + c.name + "] " + path;
                s.samples = data.count;
                s.last = data.last;
                s.average = data.average;
                s.period_max = std::max(data.current_max, data.previous_max);
                const std::size_t start = (data.head + kHistoryLength - data.count) % kHistoryLength;
                s.history.reserve(data.count);
                for (std::size_t i = 0; i < data.count; ++i)
                    s.history.push_back(data.history[(start + i) % kHistoryLength]);
                std::vector<double> sorted = s.history;
                std::ranges::sort(sorted);
                s.p50 = percentile(sorted, 0.50);
                s.p95 = percentile(sorted, 0.95);
                result.push_back(std::move(s));
            }
        });
        std::ranges::sort(result, { }, &ScopeStats::name);
        return result;
    }

    std::string tsv_snapshot()
    {
        std::string out = "scope\tsamples\tlastMs\taverageMs\tp50Ms\tp95Ms\tperiodMaxMs\n";
        for (const auto& s : stats())
        {
            out += s.name;
            out += '\t';
            out += std::to_string(s.samples);
            for (const double v : { s.last, s.average, s.p50, s.p95, s.period_max })
            {
                out += '\t';
                append_fixed(out, v);
            }
            out += '\n';
        }
        return out;
    }

} // namespace rct3::profiling
