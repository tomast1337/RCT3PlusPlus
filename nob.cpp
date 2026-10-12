// SPDX-License-Identifier: GPL-3.0-or-later
//
// Build script. Bootstrap once with:  g++ -std=c++26 -o nob nob.cpp
//
//   ./nob [build|test|clean] [--release] [--sanitize] [--filter TEXT] [-- TEST_ARGS...]
//   ./nob app|run [--release] [-- APP_ARGS...]    graphical viewer (needs GLFW, Vulkan, glslc)
//   ./nob fuzz [TARGET] [--seconds N]
//   ./nob format [--check]                        clang-format our sources (never third_party/)
//
// The audio module (src/audio/, vendored miniaudio) is compiled into the tests and the tools that use it
// only. It needs no audio device or system audio package.
//
// Environment: CXX overrides the compiler (default g++). Fuzzing needs Clang: CLANGXX (default clang++).
#include "nob.hpp"

#include <array>
#include <fstream>

using namespace nob;

namespace
{

    // Where everything we generate goes. Git-ignored. Each mode gets its own subdirectory so objects built
    // with different flags never mix.
    const fs::path kBuildDir = "build";

    struct Config
    {
        std::string mode; // "debug", "release", "asan" or "fuzz"
        std::string cxx;
        std::vector<std::string> compile_flags;
        std::vector<std::string> link_flags;

        [[nodiscard]] fs::path out_dir() const
        {
            return kBuildDir / mode;
        }
        [[nodiscard]] fs::path obj_dir() const
        {
            return out_dir() / "obj";
        }
        [[nodiscard]] fs::path test_binary() const
        {
            return out_dir() / "rct3_tests";
        }
    };

    Config make_config(bool release, bool sanitize, bool fuzz)
    {
        const char* cxx = std::getenv("CXX");
        Config c;
        c.cxx = cxx ? cxx : "g++";
        c.compile_flags = { "-std=c++26", "-Wall", "-Wextra", "-Wpedantic", "-Isrc", "-isystem", "third_party" };

        if (fuzz)
        {
            // libFuzzer ships with Clang only. Objects get coverage instrumentation, the link adds the driver.
            const char* clangxx = std::getenv("CLANGXX");
            c.cxx = clangxx ? clangxx : "clang++";
            c.mode = "fuzz";
            c.compile_flags.insert(c.compile_flags.end(), { "-O1", "-g", "-fsanitize=fuzzer-no-link,address,undefined", "-fno-omit-frame-pointer" });
            c.link_flags = { "-fsanitize=fuzzer,address,undefined" };
        }
        else if (sanitize)
        {
            c.mode = "asan";
            const std::vector<std::string> san = { "-fsanitize=address,undefined", "-fno-omit-frame-pointer" };
            c.compile_flags.insert(c.compile_flags.end(), { "-O1", "-g" });
            c.compile_flags.insert(c.compile_flags.end(), san.begin(), san.end());
            c.link_flags = san;
        }
        else if (release)
        {
            c.mode = "release";
            c.compile_flags.insert(c.compile_flags.end(), { "-O2", "-DNDEBUG" });
        }
        else
        {
            c.mode = "debug";
            c.compile_flags.insert(c.compile_flags.end(), { "-O0", "-g" });
        }
        return c;
    }

    // One entry of compile_commands.json.
    struct CompileEntry
    {
        fs::path file;
        std::vector<std::string> args;
    };

    // src/ovl/ovl.cpp -> build/<mode>/obj/src_ovl_ovl.o
    fs::path object_path(const Config& cfg, const fs::path& source)
    {
        std::string name = fs::path(source).replace_extension().generic_string();
        std::ranges::replace(name, '/', '_');
        return cfg.obj_dir() / (name + ".o");
    }

    Cmd compile_cmd(const Config& cfg, const fs::path& source, const fs::path& object)
    {
        Cmd cmd;
        cmd << cfg.cxx;
        cmd.append(cfg.compile_flags);
        cmd << "-c" << source.string() << "-o" << object.string();
        return cmd;
    }

    // Compiles sources in parallel. Objects are rebuilt when the source or any header is newer.
    bool compile(const Config& cfg, const std::vector<fs::path>& sources, const std::vector<fs::path>& headers, std::vector<fs::path>& objects, std::vector<CompileEntry>& database)
    {
        Procs procs;
        bool ok = true;
        for (const auto& source : sources)
        {
            const fs::path object = object_path(cfg, source);
            objects.push_back(object);

            const Cmd cmd = compile_cmd(cfg, source, object);
            database.push_back({ source, cmd.args });

            std::vector<fs::path> deps = headers;
            deps.push_back(source);
            if (!needs_rebuild(object, deps))
                continue;

            if (auto pid = start(cmd))
                procs.add(*pid);
            else
                ok = false;
        }
        return procs.wait_all() && ok;
    }

    // Links `objects` into `output`. The object list is remembered next to the output so that removing a
    // source file (which leaves every remaining object older than the binary) still triggers a relink.
    bool link(const Config& cfg, const fs::path& output, const std::vector<fs::path>& objects)
    {
        fs::path list_file = output;
        list_file += ".objects";

        std::string list;
        for (const auto& o : objects)
            list += o.string() + "\n";

        std::string previous;
        if (std::ifstream in(list_file); in)
            previous.assign(std::istreambuf_iterator<char>(in), { });

        if (previous == list && !needs_rebuild(output, objects))
            return true;

        Cmd cmd;
        cmd << cfg.cxx;
        cmd.append(cfg.link_flags);
        cmd << "-o" << output.string();
        for (const auto& o : objects)
            cmd << o.string();
        if (!run(cmd))
            return false;

        std::ofstream(list_file) << list;
        return true;
    }

    std::string json_escape(std::string_view s)
    {
        std::string out;
        for (char c : s)
        {
            if (c == '"' || c == '\\')
                out += '\\';
            out += c;
        }
        return out;
    }

    void write_compile_commands(const std::vector<CompileEntry>& database)
    {
        const fs::path cwd = fs::current_path();
        std::ofstream out(kBuildDir / "compile_commands.json");
        out << "[\n";
        for (std::size_t i = 0; i < database.size(); ++i)
        {
            const auto& e = database[i];
            out << "  {\"directory\": \"" << json_escape(cwd.string()) << "\", \"file\": \"" << json_escape(e.file.string()) << "\", \"arguments\": [";
            for (std::size_t j = 0; j < e.args.size(); ++j)
            {
                out << (j ? ", " : "") << '"' << json_escape(e.args[j]) << '"';
            }
            out << "]}" << (i + 1 < database.size() ? "," : "") << "\n";
        }
        out << "]\n";
    }

    std::vector<fs::path> all_headers()
    {
        std::vector<fs::path> headers;
        for (const auto& [dir, ext] : { std::pair { "src", ".hpp" }, std::pair { "tests", ".hpp" }, std::pair { "third_party", ".h" } })
        {
            const auto found = find_files(dir, ext);
            headers.insert(headers.end(), found.begin(), found.end());
        }
        return headers;
    }

    // The audio module (src/audio/) is its own target: it pulls in vendored miniaudio and its link flags, so
    // the core library, the fuzz targets and the viewer build without it. Tools that include "audio/..." and
    // the test binary link it. It needs no audio device (miniaudio loads backends at run time, and the tests
    // use its offline mode), only -ldl -lpthread -lm.
    bool is_audio_source(const fs::path& p)
    {
        return p.generic_string().starts_with("src/audio/");
    }

    std::vector<fs::path> core_sources()
    {
        std::vector<fs::path> out;
        for (auto& p : find_files("src", ".cpp"))
        {
            if (!is_audio_source(p))
                out.push_back(std::move(p));
        }
        return out;
    }

    bool uses_audio(const fs::path& source)
    {
        std::ifstream in(source);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.starts_with("#include \"audio/"))
                return true;
        }
        return false;
    }

    // Compiles src/audio/*.cpp and the miniaudio implementation unit (vendored code, warnings off).
    bool compile_audio(const Config& cfg, const std::vector<fs::path>& headers, std::vector<fs::path>& objects, std::vector<CompileEntry>& database)
    {
        std::vector<fs::path> sources;
        for (auto& p : find_files("src", ".cpp"))
        {
            if (is_audio_source(p))
                sources.push_back(std::move(p));
        }
        if (!compile(cfg, sources, headers, objects, database))
            return false;

        Config vendored = cfg;
        vendored.compile_flags.push_back("-w");
        return compile(vendored, { "third_party/miniaudio/miniaudio_impl.cpp" }, headers, objects, database);
    }

    Config with_audio_link(Config cfg)
    {
        cfg.link_flags.insert(cfg.link_flags.end(), { "-ldl", "-lpthread", "-lm" });
        return cfg;
    }

    // Library objects, tools (tools/<name>/main.cpp -> build/<mode>/<name>) and the single test binary.
    bool build_all(const Config& cfg)
    {
        if (!mkdir_p(cfg.obj_dir()))
            return false;

        const auto headers = all_headers();
        std::vector<CompileEntry> database;

        std::vector<fs::path> lib_objects;
        if (!compile(cfg, core_sources(), headers, lib_objects, database))
            return false;

        std::vector<fs::path> audio_objects;
        if (!compile_audio(cfg, headers, audio_objects, database))
            return false;

        for (const auto& main_cpp : find_files("tools", ".cpp"))
        {
            const bool audio = uses_audio(main_cpp);
            std::vector<fs::path> objects = lib_objects;
            if (audio)
                objects.insert(objects.end(), audio_objects.begin(), audio_objects.end());
            if (!compile(cfg, { main_cpp }, headers, objects, database))
                return false;
            if (!link(audio ? with_audio_link(cfg) : cfg, cfg.out_dir() / main_cpp.parent_path().filename(), objects))
                return false;
        }

        // All of tests/**/*.cpp (the doctest runner plus every *.test.cpp) links into one binary, together
        // with the audio module.
        std::vector<fs::path> test_objects = lib_objects;
        test_objects.insert(test_objects.end(), audio_objects.begin(), audio_objects.end());
        if (!compile(cfg, find_files("tests", ".cpp"), headers, test_objects, database))
            return false;
        if (!link(with_audio_link(cfg), cfg.test_binary(), test_objects))
            return false;

        write_compile_commands(database);
        return true;
    }

    // The graphical viewer in app/. Not part of `build` so the library and tests never need GLFW or Vulkan.
    // Shaders (app/shaders/NAME.{vert,frag}) are compiled with glslc into C arrays included by the sources.
    bool build_app(const Config& cfg, fs::path& binary)
    {
        if (!mkdir_p(cfg.obj_dir()))
            return false;
        const fs::path gen_dir = cfg.out_dir() / "gen";
        if (!mkdir_p(gen_dir))
            return false;

        for (const auto& shader : find_files("app/shaders", ".vert"))
        {
            for (const char* ext : { ".vert", ".frag" })
            {
                fs::path source = shader;
                source.replace_extension(ext);
                if (!fs::exists(source))
                    continue;
                const fs::path output = gen_dir / (source.filename().string() + ".inc");
                if (!needs_rebuild(output, std::array<fs::path, 1> { source }))
                    continue;
                Cmd cmd;
                cmd << "glslc" << "-mfmt=c" << "--target-env=vulkan1.3" << source.string() << "-o" << output.string();
                if (!run(cmd))
                    return false;
            }
        }

        const auto headers = all_headers();
        std::vector<CompileEntry> ignored;

        // Dear ImGui is vendored as-is; its warnings are not ours.
        Config third_party = cfg;
        third_party.compile_flags.insert(third_party.compile_flags.end(), { "-w", "-isystem", "third_party/imgui", "-isystem", "third_party/vulkan" });
        std::vector<fs::path> imgui_sources = find_files("third_party/imgui", ".cpp");
        std::vector<fs::path> objects;
        if (!compile(third_party, imgui_sources, headers, objects, ignored))
            return false;

        Config app = cfg;
        app.compile_flags.insert(app.compile_flags.end(), { "-Wno-missing-field-initializers", "-isystem", "third_party/imgui", "-isystem", "third_party/vulkan", "-Iapp", "-I" + gen_dir.string() });
        std::vector<fs::path> lib_sources = core_sources();
        std::vector<fs::path> app_headers = headers;
        for (const auto& h : find_files("app", ".hpp"))
            app_headers.push_back(h);
        for (const auto& inc : find_files(gen_dir, ".inc"))
            app_headers.push_back(inc);
        if (!compile(app, lib_sources, app_headers, objects, ignored))
            return false;
        if (!compile(app, find_files("app", ".cpp"), app_headers, objects, ignored))
            return false;

        binary = cfg.out_dir() / "rct3pp";
        Config linker = cfg;
        linker.link_flags.insert(linker.link_flags.end(), { "-lglfw", "-lvulkan" });
        return link(linker, binary, objects);
    }

    // Fuzz targets: fuzz/<name>.cpp -> build/fuzz/<name>, linked with the library objects.
    bool build_fuzz(const Config& cfg, std::vector<fs::path>& targets)
    {
        if (!mkdir_p(cfg.obj_dir()))
            return false;

        const auto headers = all_headers();
        std::vector<CompileEntry> database;

        std::vector<fs::path> lib_objects;
        if (!compile(cfg, core_sources(), headers, lib_objects, database))
            return false;

        for (const auto& source : find_files("fuzz", ".cpp"))
        {
            std::vector<fs::path> objects = lib_objects;
            if (!compile(cfg, { source }, headers, objects, database))
                return false;
            const fs::path binary = cfg.out_dir() / source.stem();
            if (!link(cfg, binary, objects))
                return false;
            targets.push_back(binary);
        }
        return true;
    }

    // Formats our own sources with clang-format using the style in .clang-format. With `check` it only reports
    // files that would change. Vendored code in third_party/ is never touched. CLANGFORMAT overrides the binary.
    bool run_format(bool check)
    {
        std::vector<fs::path> files;
        for (const char* dir : { "src", "tests", "tools", "fuzz", "app" })
        {
            if (!fs::exists(dir))
                continue;
            for (const char* ext : { ".cpp", ".hpp", ".h" })
            {
                const auto found = find_files(dir, ext);
                files.insert(files.end(), found.begin(), found.end());
            }
        }
        files.push_back("nob.cpp");
        files.push_back("nob.hpp");
        const char* binary = std::getenv("CLANGFORMAT");
        Cmd cmd;
        cmd << (binary ? binary : "clang-format") << "--style=file";
        if (check)
            cmd << "--dry-run" << "-Werror";
        else
            cmd << "-i";
        for (const auto& f : files)
            cmd << f.string();
        return run(cmd);
    }

    // Runs one fuzz target for `seconds`. New inputs land in the git-ignored local/ directory, crashes too.
    bool run_fuzz(const fs::path& binary, int seconds)
    {
        const std::string name = binary.filename().string();
        const fs::path corpus = fs::path("local") / "fuzz-corpus" / name;
        const fs::path crashes = fs::path("local") / "fuzz-crashes" / name;
        if (!mkdir_p(corpus) || !mkdir_p(crashes))
            return false;

        Cmd cmd;
        cmd << binary.string() << std::format("-max_total_time={}", seconds) << std::format("-artifact_prefix={}/", crashes.string()) << "-print_final_stats=1";
        if (fs::exists("fuzz/ovl.dict"))
            cmd << "-dict=fuzz/ovl.dict";
        cmd << corpus.string();
        const fs::path seeds = fs::path("fuzz") / "seeds" / name;
        if (fs::exists(seeds))
            cmd << seeds.string();
        return run(cmd);
    }

} // namespace

int main(int argc, char** argv)
{
    go_rebuild_urself(argv, __FILE__, std::array<fs::path, 1> { "nob.hpp" });

    std::string command = "build";
    bool release = false;
    bool sanitize = false;
    bool check = false;
    std::vector<std::string> test_args;
    std::string fuzz_target;
    int fuzz_seconds = 30;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--release")
            release = true;
        else if (arg == "--sanitize")
            sanitize = true;
        else if (arg == "--seconds" && i + 1 < argc)
            fuzz_seconds = std::atoi(argv[++i]);
        else if (arg == "--filter" && i + 1 < argc)
            test_args.push_back(std::format("-tc=*{}*", argv[++i]));
        else if (arg == "--")
        {
            for (++i; i < argc; ++i)
                test_args.emplace_back(argv[i]);
        }
        else if (arg == "build" || arg == "test" || arg == "clean" || arg == "fuzz" || arg == "app" || arg == "run" || arg == "format")
            command = arg;
        else if (arg == "--check")
            check = true;
        else if (command == "fuzz" && !arg.starts_with("-"))
            fuzz_target = arg;
        else
        {
            log(Level::Error,
                "unknown argument '{}'. usage: ./nob [build|test|clean] [--release] [--sanitize] "
                "[--filter TEXT] [-- TEST_ARGS...] | ./nob fuzz [TARGET] [--seconds N] | ./nob format [--check]",
                arg);
            return 2;
        }
    }

    if (command == "clean")
    {
        std::error_code ec;
        fs::remove_all(kBuildDir, ec);
        return ec ? 1 : 0;
    }

    if (command == "format")
        return run_format(check) ? 0 : 1;

    if (command == "fuzz")
    {
        const Config cfg = make_config(false, false, true);
        std::vector<fs::path> targets;
        if (!build_fuzz(cfg, targets))
            return 1;
        for (const auto& t : targets)
        {
            if (!fuzz_target.empty() && t.filename() != fuzz_target)
                continue;
            if (!run_fuzz(t, fuzz_seconds))
                return 1;
        }
        return 0;
    }

    const Config cfg = make_config(release, sanitize, false);

    if (command == "app" || command == "run")
    {
        fs::path binary;
        if (!build_app(cfg, binary))
            return 1;
        if (command == "app")
            return 0;
        Cmd run_cmd;
        run_cmd << binary.string();
        run_cmd.append(test_args);
        return run(run_cmd) ? 0 : 1;
    }

    if (!build_all(cfg))
        return 1;
    if (command != "test")
        return 0;

    Cmd cmd;
    cmd << cfg.test_binary().string();
    cmd.append(test_args);
    return run(cmd) ? 0 : 1;
}
