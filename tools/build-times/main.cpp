// SPDX-License-Identifier: GPL-3.0-or-later
//
// build-times: measure build times, to compare the tree before and after a change to how it is built.
//
//   build-times [clean] [touch] [report] [-j 4] [--repeat 2] [--ci-runs 10] [--no-ci] [--out build/build-times]
//
// Run from the repository root with ./nob bootstrapped. Deletes build/debug, then for each repeat builds `./nob test`
// and `./nob app` from clean. Afterwards touches a few files one at a time and rebuilds both, counting the compiler
// runs. Reports wall time, CPU time and peak memory of each step, how many translation units include each header
// (from the compiler's .d files), and the CI job times of recent runs (needs `gh`). Writes results.md and
// results.json into --out.
//
// Without steps it does all three from scratch. Given steps, it adds to the results.json already in --out, so a long
// measurement can be split: `build-times clean --repeat 1` twice, then `build-times touch report`.
//
// The binary lives in build/debug, which a clean step deletes, so copy it out first:
//   cp build/debug/build-times build/ && systemd-run --user --scope build/build-times
// CPU time and peak memory come from the cgroup the tool runs in, hence the scope of its own. The load average before
// and after each step shows how busy the machine was.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <print>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace
{
    constexpr std::string_view kTouched[] = { "src/ovl/costume.cpp", "src/park/turns.hpp", "src/park/pools/pools.hpp", "src/park/pools/pools.cpp" };
    constexpr std::string_view kOwnDirs[] = { "src/", "app/", "tools/", "tests/" };

    std::string read_file(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }

    double round1(double v)
    {
        return static_cast<double>(std::llround(v * 10.0)) / 10.0;
    }
    double round2(double v)
    {
        return static_cast<double>(std::llround(v * 100.0)) / 100.0;
    }

    // The value after `key ` in a cgroup stat file, 0 when missing.
    std::int64_t cgroup_stat(const std::optional<fs::path>& cgroup, std::string_view file, std::string_view key)
    {
        if (!cgroup)
            return 0;
        std::istringstream lines(read_file(*cgroup / file));
        for (std::string name; lines >> name;)
        {
            std::int64_t value = 0;
            lines >> value;
            if (name == key)
                return value;
        }
        return 0;
    }

    double load_average()
    {
        std::ifstream in("/proc/loadavg");
        double load = 0;
        in >> load;
        return load;
    }

    std::string clock_time(std::chrono::system_clock::time_point t)
    {
        return std::format("{:%H:%M:%S}", std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(t)));
    }

    // Runs `cmd` through the shell and returns wall s, CPU s, peak MiB, compiler runs and load before and after. The
    // peak is the cgroup's anonymous memory, sampled: memory.peak would count the page cache the build's output fills.
    // Exits on failure.
    Json run(const std::string& cmd, const std::optional<fs::path>& cgroup, const fs::path& out_dir)
    {
        const double load_before = load_average();
        const std::int64_t cpu_before = cgroup_stat(cgroup, "cpu.stat", "usage_usec");
        std::atomic<std::int64_t> peak = 0;
        std::atomic<bool> done = false;
        const fs::path log_path = out_dir / "last_run.log";
        const auto start = std::chrono::system_clock::now();
        const auto t0 = std::chrono::steady_clock::now();
        int status = 0;
        {
            std::jthread sampler([&] {
                while (!done)
                {
                    peak = std::max(peak.load(), cgroup_stat(cgroup, "memory.stat", "anon"));
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            });
            status = std::system(std::format("{} > '{}' 2>&1", cmd, log_path.string()).c_str());
            done = true;
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const std::string log = read_file(log_path);
        if (status != 0)
        {
            std::println(stderr, "{} failed:\n{}", cmd, log.substr(log.size() > 4000 ? log.size() - 4000 : 0));
            std::exit(1);
        }
        int compiles = 0;
        std::istringstream lines(log);
        for (std::string line; std::getline(lines, line);)
            compiles += line.contains("CMD:") && line.contains(" -c ") ? 1 : 0;
        Json r = { { "cmd", cmd }, { "wall", round1(wall) } };
        r["cpu"] = cgroup ? Json(round1(static_cast<double>(cgroup_stat(cgroup, "cpu.stat", "usage_usec") - cpu_before) / 1e6)) : Json(nullptr);
        r["peak_mib"] = cgroup ? Json(std::llround(static_cast<double>(peak.load()) / (1 << 20))) : Json(nullptr);
        r["tus"] = compiles;
        r["load"] = { round2(load_before), round2(load_average()) };
        r["start"] = clock_time(start);
        r["end"] = clock_time(std::chrono::system_clock::now());
        return r;
    }

    // Standard output of `cmd`, through a file next to the results.
    std::string capture(const std::string& cmd, const fs::path& out_dir)
    {
        const fs::path path = out_dir / "capture.txt";
        if (std::system(std::format("{} > '{}'", cmd, path.string()).c_str()) != 0)
        {
            std::println(stderr, "{} failed", cmd);
            std::exit(1);
        }
        return read_file(path);
    }

    void save(const Json& results, const fs::path& out_dir)
    {
        std::ofstream(out_dir / "results.json") << results.dump(1) << "\n";
    }

    std::vector<std::pair<std::string, int>> most_common(const std::map<std::string, int>& counts)
    {
        std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
        std::ranges::stable_sort(sorted, [](const auto& a, const auto& b) { return a.second > b.second; });
        return sorted;
    }

    std::string cell(const Json& v)
    {
        return v.is_null() || v == 0 ? std::string("-") : v.dump();
    }
} // namespace

int main(int argc, char** argv)
{
    std::set<std::string> steps;
    int jobs = 4;
    int repeat = 2;
    int ci_runs = 10;
    bool ci = true;
    fs::path out_dir = "build/build-times";
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a = argv[i];
        const bool has_value = i + 1 < argc;
        if (a == "clean" || a == "touch" || a == "report")
            steps.emplace(a);
        else if ((a == "-j" || a == "--jobs") && has_value)
            jobs = std::atoi(argv[++i]);
        else if (a == "--repeat" && has_value)
            repeat = std::atoi(argv[++i]);
        else if (a == "--ci-runs" && has_value)
            ci_runs = std::atoi(argv[++i]);
        else if (a == "--no-ci")
            ci = false;
        else if (a == "--out" && has_value)
            out_dir = argv[++i];
        else
        {
            std::println(stderr, "usage: build-times [clean] [touch] [report] [-j N] [--repeat N] [--ci-runs N] [--no-ci] [--out DIR]");
            return 2;
        }
    }
    const bool all = steps.empty();
    if (all)
        steps = { "clean", "touch", "report" };
    if (!fs::exists("nob"))
    {
        std::println(stderr, "run from the repository root after bootstrapping nob");
        return 1;
    }
    fs::create_directories(out_dir);

    // The cgroup this process runs in, when it has memory accounting.
    std::optional<fs::path> cgroup;
    {
        std::istringstream lines(read_file("/proc/self/cgroup"));
        std::string last;
        for (std::string line; std::getline(lines, line);)
            if (!line.empty())
                last = line;
        if (const auto colon = last.find(':', last.find(':') + 1); colon != std::string::npos)
            if (fs::path path = "/sys/fs/cgroup" + last.substr(colon + 1); fs::exists(path / "memory.stat"))
                cgroup = path;
    }

    const std::string j = std::format("-j {}", jobs);
    Json results;
    if (!all && fs::exists(out_dir / "results.json"))
        results = Json::parse(read_file(out_dir / "results.json"));
    else
    {
        std::string commit = capture("git rev-parse --short HEAD", out_dir);
        std::erase(commit, '\n');
        results = { { "jobs", jobs }, { "date", std::format("{:%Y-%m-%d %H:%M}", std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::floor<std::chrono::minutes>(std::chrono::system_clock::now()))) }, { "commit", commit }, { "clean", Json::array() }, { "touch", Json::array() } };
    }

    // 1. Clean builds. The app is also built from clean, so neither profits from the other's objects.
    for (int i = 0; i < (steps.contains("clean") ? repeat : 0); ++i)
        for (const std::string target : { "test", "app" })
        {
            fs::remove_all("build/debug");
            Json r = run(std::format("./nob {} {}", target, j), cgroup, out_dir);
            std::println("clean {}: {}", target, r.dump());
            std::fflush(stdout);
            Json row = { { "target", target } };
            row.update(r);
            results["clean"].push_back(row);
            save(results, out_dir);
        }

    // 2. One-file rebuilds, from both targets up to date.
    if (steps.contains("touch"))
    {
        results["touch"] = Json::array();
        run(std::format("./nob test {}", j), cgroup, out_dir);
        run(std::format("./nob app {}", j), cgroup, out_dir);
        for (const std::string_view path : kTouched)
        {
            fs::last_write_time(path, fs::file_time_type::clock::now());
            for (const std::string target : { "test", "app" })
            {
                Json r = run(std::format("./nob {} {}", target, j), cgroup, out_dir);
                std::println("touch {} {}: {}", path, target, r.dump());
                std::fflush(stdout);
                Json row = { { "file", path }, { "target", target } };
                row.update(r);
                results["touch"].push_back(row);
            }
        }
    }
    save(results, out_dir);
    if (!steps.contains("report"))
        return 0;

    // 3. Includes: how many translation units include each own header, directly or not, and each standard header one
    //    of our files in the unit names. The .d files leave system headers out, so the standard headers only those pull
    //    in are not counted. Then how many of our files #include each own header by name.
    const auto own_file = [](std::string_view w) { return std::ranges::any_of(kOwnDirs, [&](std::string_view d) { return w.starts_with(d); }); };
    std::map<std::string, int> own;
    std::map<std::string, int> standard;
    std::map<std::string, int> direct;
    int tus = 0;
    {
        const std::regex angled(R"(^\s*#\s*include\s*<([^>]+)>)", std::regex::multiline);
        std::map<std::string, std::set<std::string>> named; // own file -> standard headers it names
        for (const auto& entry : fs::directory_iterator("build/debug/obj"))
        {
            if (entry.path().extension() != ".d")
                continue;
            std::string text = read_file(entry.path());
            for (std::size_t at = 0; (at = text.find("\\\n", at)) != std::string::npos;)
                text.replace(at, 2, " ");
            std::istringstream split(text);
            std::vector<std::string> words;
            for (std::string w; split >> w;)
                if (!w.ends_with(':'))
                    words.push_back(w);
            if (words.empty())
                continue;
            ++tus;
            std::set<std::string> reached;
            for (const std::string& w : std::set<std::string>(words.begin(), words.end()))
            {
                if (!own_file(w))
                    continue;
                if (w != words.front())
                    ++own[w];
                if (!named.contains(w))
                {
                    const std::string source = read_file(w);
                    auto& names = named[w];
                    for (auto m = std::sregex_iterator(source.begin(), source.end(), angled); m != std::sregex_iterator(); ++m)
                        names.insert((*m)[1].str());
                }
                reached.insert(named[w].begin(), named[w].end());
            }
            for (const std::string& h : reached)
                ++standard[h];
        }
        const std::regex quoted(R"re(^\s*#\s*include\s*"([^"]+)")re", std::regex::multiline);
        for (const char* root : { "src", "app", "tools", "tests" })
            for (const auto& entry : fs::recursive_directory_iterator(root))
            {
                const auto ext = entry.path().extension();
                if (ext != ".cpp" && ext != ".hpp" && ext != ".h")
                    continue;
                const std::string source = read_file(entry.path());
                for (auto m = std::sregex_iterator(source.begin(), source.end(), quoted); m != std::sregex_iterator(); ++m)
                    ++direct[(*m)[1].str()];
            }
    }
    const auto own_sorted = most_common(own);
    const auto standard_sorted = most_common(standard);
    results["includes"] = { { "tus", tus }, { "own", own_sorted }, { "std", standard_sorted }, { "direct", most_common(direct) } };

    // 4. CI: median and range of each job's minutes over the last successful runs.
    if (ci)
    {
        const Json listed = Json::parse(capture(std::format("gh run list --workflow ci.yml --status success --limit {} --json databaseId", ci_runs), out_dir));
        std::map<std::string, std::vector<double>> minutes;
        for (const Json& r : listed)
            for (const Json& job : Json::parse(capture(std::format("gh run view {} --json jobs", r["databaseId"].get<std::int64_t>()), out_dir))["jobs"])
            {
                if (!job.value("startedAt", Json()).is_string() || !job.value("completedAt", Json()).is_string() || job.value("conclusion", "") != "success")
                    continue;
                const auto parse = [](const std::string& s) {
                    std::chrono::sys_seconds t;
                    std::istringstream in(s);
                    in >> std::chrono::parse("%Y-%m-%dT%H:%M:%SZ", t);
                    return t;
                };
                minutes[job["name"].get<std::string>()].push_back(std::chrono::duration<double>(parse(job["completedAt"]) - parse(job["startedAt"])).count() / 60);
            }
        Json table = Json::object();
        for (auto& [name, m] : minutes)
        {
            std::ranges::sort(m);
            const double median = m.size() % 2 ? m[m.size() / 2] : (m[m.size() / 2 - 1] + m[m.size() / 2]) / 2;
            table[name] = { { "median", round1(median) }, { "min", round1(m.front()) }, { "max", round1(m.back()) }, { "runs", m.size() } };
        }
        results["ci"] = table;
    }

    // 5. Report.
    std::map<std::string, std::size_t> best; // target -> index of its fastest clean build
    for (std::size_t i = 0; i < results["clean"].size(); ++i)
    {
        const Json& c = results["clean"][i];
        const std::string target = c["target"];
        if (!best.contains(target) || c["wall"].get<double>() < results["clean"][best[target]]["wall"].get<double>())
            best[target] = i;
    }
    std::string md = std::format("Build times at {}, {}, `-j {}`, debug.\n\n| step | wall s | CPU s | peak MiB | TUs | load before/after |\n|---|---|---|---|---|---|\n", results["commit"].get<std::string>(), results["date"].get<std::string>(), jobs);
    const auto row = [&](std::string_view step, const Json& r) { md += std::format("| {} | {} | {} | {} | {} | {} / {} |\n", step, r["wall"].dump(), r["cpu"].is_null() ? "-" : r["cpu"].dump(), cell(r["peak_mib"]), r["tus"].dump(), r["load"][0].dump(), r["load"][1].dump()); };
    for (std::size_t i = 0; i < results["clean"].size(); ++i)
    {
        const Json& c = results["clean"][i];
        row(std::format("clean `nob {}`{}", c["target"].get<std::string>(), best[c["target"]] == i ? " (best)" : ""), c);
    }
    for (const Json& t : results["touch"])
        row(std::format("touch `{}`, `nob {}`", t["file"].get<std::string>(), t["target"].get<std::string>()), t);
    md += std::format("\nTranslation units including each header ({} TUs, from the .d files), and direct `#include`s of it:\n\n| own header | TUs | direct |\n|---|---|---|\n", tus);
    for (std::size_t i = 0; i < std::min<std::size_t>(25, own_sorted.size()); ++i)
    {
        const auto& [h, n] = own_sorted[i];
        const std::string short_name = h.starts_with("src/") ? h.substr(4) : h;
        md += std::format("| `{}` | {} | {} |\n", h, n, direct.contains(short_name) ? direct[short_name] : direct[h]);
    }
    md += "\n| standard header (named by our files) | TUs |\n|---|---|\n";
    for (std::size_t i = 0; i < std::min<std::size_t>(15, standard_sorted.size()); ++i)
        md += std::format("| `<{}>` | {} |\n", standard_sorted[i].first, standard_sorted[i].second);
    if (results.contains("ci"))
    {
        md += std::format("\nCI jobs over the last {} successful runs, minutes:\n\n| job | median | min | max | runs |\n|---|---|---|---|---|\n", ci_runs);
        for (const auto& [name, v] : results["ci"].items())
            md += std::format("| {} | {} | {} | {} | {} |\n", name, v["median"].dump(), v["min"].dump(), v["max"].dump(), v["runs"].dump());
    }
    save(results, out_dir);
    std::ofstream(out_dir / "results.md") << md;
    std::print("{}", md);
    return 0;
}
