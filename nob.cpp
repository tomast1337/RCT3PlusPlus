// SPDX-License-Identifier: GPL-3.0-or-later
//
// Build script. Bootstrap once with:  g++ -std=c++26 -o nob nob.cpp
//
//   ./nob [build|test|clean] [--release] [--sanitize] [--no-tools] [--filter TEXT] [-- TEST_ARGS...]
//   ./nob app|run [--release] [-- APP_ARGS...]    graphical viewer (needs GLFW, Vulkan, slangc)
//   ./nob script [FILE.ts|FILE.js] [-- APP_ARGS...] runs the viewer with the script FILE, or each of
//                                                 scripts/tests; TypeScript compiles with tsc (npm install -g
//                                                 typescript) first. Report: build/scripts/report/index.html
//   ./nob fuzz [TARGET] [--seconds N]
//   ./nob tidy                                    writes the compile database, then runs clang-tidy over src, tools and fuzz
//   ./nob format [--check]                        clang-format our sources (never third_party/)
//   ./nob fetch                                   downloads optional assets (the fallback font) into downloads/
//
// The audio module (src/audio/, vendored miniaudio) is compiled into the tests and the tools that use it
// only. It needs no audio device or system audio package.
//
// -j N (or --jobs N) limits how many compilers run at once, to keep a build from taking the whole machine;
// without it, NOB_JOBS does the same, and by default there is one per core, fewer when memory is short.
//
// Environment: CXX overrides the compiler (default g++). Fuzzing needs Clang: CLANGXX (default clang++).
// CLANGFORMAT and CLANGTIDY override the clang-format and clang-tidy binaries.
#include "nob.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <ranges>
#include <thread>

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
        std::size_t jobs = 1;       // Compilers run at once
        bool database_only = false; // Write compile_commands.json without compiling or linking, for clang-tidy

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

    // Memory a compiler may take on our heaviest files, with room to spare: about 2.5 GB was measured.
    constexpr std::size_t kJobKilobytes = 3 * 1024 * 1024;

    // How many compilers run at once: `-j N` on the command line, else NOB_JOBS, else one per core, but no more
    // than the available memory holds, so a build cannot run the machine out of memory.
    std::size_t job_count(int asked)
    {
        if (asked > 0)
            return static_cast<std::size_t>(asked);
        if (const char* env = std::getenv("NOB_JOBS"); env && std::atoi(env) > 0)
            return static_cast<std::size_t>(std::atoi(env));
        std::size_t jobs = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        // shortcut: Linux only (/proc/meminfo); elsewhere one per core, upgrade if we build on other systems.
        std::ifstream meminfo("/proc/meminfo");
        std::string key;
        std::size_t kilobytes = 0;
        while (meminfo >> key >> kilobytes)
        {
            if (key == "MemAvailable:")
                return std::clamp<std::size_t>(kilobytes / kJobKilobytes, 1, jobs);
            meminfo.ignore(64, '\n');
        }
        return jobs;
    }

    // Whether `program` is on PATH.
    bool on_path(std::string_view program)
    {
        const char* path = std::getenv("PATH");
        for (const auto dir : std::views::split(std::string_view(path ? path : ""), ':'))
        {
            std::error_code error;
            if (fs::exists(fs::path(std::string_view(dir)) / program, error))
                return true;
        }
        return false;
    }

    Config make_config(bool release, bool sanitize, bool fuzz, std::size_t jobs)
    {
        const char* cxx = std::getenv("CXX");
        Config c;
        c.jobs = jobs;
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
            // Line tables are all a sanitizer report needs; full debug info only slows the build and the link.
            c.compile_flags.insert(c.compile_flags.end(), { "-O1", "-g1" });
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
            // -Og keeps it debuggable at a fraction of -O0's cost; the park was too slow to play at -O0.
            c.compile_flags.insert(c.compile_flags.end(), { "-Og", "-g" });
        }
        // LLVM's linker, when there is one, links several times faster than the default.
        if (on_path("ld.lld"))
            c.link_flags.push_back("-fuse-ld=lld");
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
        return cfg.obj_dir() / std::format("{}.o", name);
    }

    Cmd compile_cmd(const Config& cfg, const fs::path& source, const fs::path& object)
    {
        Cmd cmd;
        cmd << cfg.cxx;
        cmd.append(cfg.compile_flags);
        fs::path depfile = object;
        depfile.replace_extension(".d");
        cmd << "-MMD" << "-MF" << depfile.string() << "-c" << source.string() << "-o" << object.string();
        return cmd;
    }

    // The files the compiler said `object` was made from, in the depfile it wrote next to it; none when it has
    // not written one yet.
    std::vector<fs::path> depfile_inputs(const fs::path& object)
    {
        fs::path depfile = object;
        depfile.replace_extension(".d");
        std::ifstream in(depfile);
        std::vector<fs::path> inputs;
        std::string word;
        // "object.o: source.cpp header.hpp \" and so on; our paths have no spaces.
        while (in >> word)
        {
            if (word != "\\" && !word.ends_with(':'))
                inputs.emplace_back(word);
        }
        return inputs;
    }

    // Compiles sources in parallel. Objects are rebuilt when the source or a header it includes is newer.
    bool compile(const Config& cfg, const std::vector<fs::path>& sources, std::vector<fs::path>& objects, std::vector<CompileEntry>& database)
    {
        Procs procs(cfg.jobs);
        bool ok = true;
        for (const auto& source : sources)
        {
            const fs::path object = object_path(cfg, source);
            objects.push_back(object);

            const Cmd cmd = compile_cmd(cfg, source, object);
            database.push_back({ source, cmd.args });
            if (cfg.database_only)
                continue;

            const std::vector<fs::path> deps = depfile_inputs(object);
            if (!deps.empty() && !needs_rebuild(object, deps))
                continue;

            procs.make_room();
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
        if (cfg.database_only)
            return true;
        fs::path list_file = output;
        list_file += ".objects";

        std::string list;
        for (const auto& o : objects)
            list += std::format("{}\n", o.string());

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
        cmd << "-lstdc++exp"; // std::stacktrace; a static library, so after the objects
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

    // The audio module (src/audio/, and the movies in src/video/) is its own target: it pulls in vendored
    // miniaudio, the FFmpeg cut and their link flags, so the core library and the fuzz targets that do not need
    // it build without it. Tools and fuzz targets that include "audio/..." or "video/...", the viewer and the
    // test binary link it. It needs no audio device (miniaudio loads backends at run time, and the tests
    // use its offline mode), only -ldl -lpthread -lm.
    bool is_audio_source(const fs::path& p)
    {
        return p.generic_string().starts_with("src/audio/") || p.generic_string().starts_with("src/video/");
    }

    // Files under assets/ are built into every binary, so the program ships as one file. A generated source
    // holds each with #embed and a table by path (relative to assets/, slashes forward); src/assets/
    // embedded_files.hpp reads it. The source is written again when the list changes or a file is newer.
    const fs::path kAssetsDir = "assets";

    std::optional<fs::path> generate_embedded_assets(const Config& cfg)
    {
        const fs::path gen_dir = cfg.out_dir() / "gen";
        if (!mkdir_p(gen_dir))
            return std::nullopt;
        const fs::path output = gen_dir / "embedded_assets.cpp";

        std::vector<fs::path> files;
        if (fs::is_directory(kAssetsDir))
        {
            for (const auto& entry : fs::recursive_directory_iterator(kAssetsDir))
            {
                if (entry.is_regular_file())
                    files.push_back(entry.path());
            }
        }
        // By the path the table is searched with.
        std::ranges::sort(files, { }, [](const fs::path& f) { return fs::relative(f, kAssetsDir).generic_string(); });

        std::string text = "// Generated by nob from assets/. Do not edit.\n#include \"assets/embedded_files.hpp\"\n\nnamespace rct3::assets\n{\n    namespace\n    {\n";
        for (std::size_t i = 0; i < files.size(); ++i)
        {
            // A zero after the bytes keeps empty files legal; it is not counted in the size.
            text += std::format("        constexpr unsigned char kFile{}[] = {{\n#embed \"{}\" suffix(, )\n            0 }};\n", i, json_escape(fs::absolute(files[i]).generic_string()));
        }
        text += std::format("    }} // namespace\n\n    std::span<const EmbeddedFile> embedded_files()\n    {{\n        // Built on first use: the byte views are not constant expressions.\n        static const std::array<EmbeddedFile, {}> kFiles {{ {{\n", files.size());
        for (std::size_t i = 0; i < files.size(); ++i)
        {
            const std::string name = fs::relative(files[i], kAssetsDir).generic_string();
            const std::string data = std::format("kFile{}", i);
            text += std::format("            {{ \"{}\", {{ reinterpret_cast<const std::byte*>({}), sizeof({}) - 1 }} }},\n", json_escape(name), data, data);
        }
        text += "        } };\n        return kFiles;\n    }\n} // namespace rct3::assets\n";

        std::string previous;
        if (std::ifstream in(output); in)
            previous.assign(std::istreambuf_iterator<char>(in), { });
        if (previous != text || needs_rebuild(output, files))
            std::ofstream(output) << text;
        return output;
    }

    std::vector<fs::path> core_sources(const Config& cfg)
    {
        std::vector<fs::path> out;
        for (auto& p : find_files("src", ".cpp"))
        {
            if (!is_audio_source(p))
                out.push_back(std::move(p));
        }
        if (auto embedded = generate_embedded_assets(cfg))
            out.push_back(std::move(*embedded));
        return out;
    }

    bool uses_audio(const fs::path& source)
    {
        std::ifstream in(source);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.starts_with("#include \"audio/") || line.starts_with("#include \"video/"))
                return true;
        }
        return false;
    }

    // Compiles src/audio/*.cpp and the miniaudio implementation unit (vendored code, warnings off).
    // The same build for C sources: the C compiler that goes with the C++ one, C17, no warnings (vendored code
    // only), plus `flags`.
    Config c_config(const Config& cfg, const std::vector<std::string>& flags)
    {
        Config c = cfg;
        if (const char* cc = std::getenv("CC"))
            c.cxx = cc;
        else if (cfg.cxx.ends_with("clang++"))
            c.cxx = cfg.cxx.substr(0, cfg.cxx.size() - 2);
        else if (cfg.cxx.ends_with("g++"))
            c.cxx = std::format("{}gcc", cfg.cxx.substr(0, cfg.cxx.size() - 3));
        else
            c.cxx = "cc";
        c.compile_flags.clear();
        for (const auto& f : cfg.compile_flags)
        {
            if (f == "-std=c++26")
                c.compile_flags.push_back("-std=gnu17");
            else if (!f.starts_with("-W"))
                c.compile_flags.push_back(f);
        }
        c.compile_flags.push_back("-w");
        c.compile_flags.insert(c.compile_flags.end(), flags.begin(), flags.end());
        return c;
    }

    bool compile_audio(const Config& cfg, std::vector<fs::path>& objects, std::vector<CompileEntry>& database)
    {
        std::vector<fs::path> sources;
        for (auto& p : find_files("src", ".cpp"))
        {
            if (is_audio_source(p))
                sources.push_back(std::move(p));
        }
        if (!compile(cfg, sources, objects, database))
            return false;

        Config vendored = cfg;
        vendored.compile_flags.push_back("-w");
        if (!compile(vendored, { "third_party/miniaudio/miniaudio_impl.cpp" }, objects, database))
            return false;
        // The FFmpeg cut and our parts of it: the sources its sources.txt lists (the others are included by
        // them), with the defines FFmpeg's own build compiles them with.
        std::vector<fs::path> ffmpeg;
        std::ifstream list("third_party/ffmpeg/sources.txt");
        for (std::string line; std::getline(list, line);)
        {
            if (!line.empty())
                ffmpeg.push_back(fs::path("third_party/ffmpeg") / line);
        }
        if (ffmpeg.empty())
        {
            log(Level::Error, "third_party/ffmpeg/sources.txt is missing or empty");
            return false;
        }
        return compile(c_config(cfg, { "-Ithird_party/ffmpeg", "-DHAVE_AV_CONFIG_H", "-D_ISOC11_SOURCE", "-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE_SOURCE", "-D_POSIX_C_SOURCE=200112", "-D_XOPEN_SOURCE=600" }), ffmpeg, objects, database);
    }

    // Vendored code every target links: the stb_truetype and stb_image implementation units and the QuickJS-NG
    // script engine, built without our warnings.
    bool compile_vendored_core(const Config& cfg, std::vector<fs::path>& objects, std::vector<CompileEntry>& database)
    {
        Config vendored = cfg;
        vendored.compile_flags.push_back("-w");
        if (!compile(vendored, { "third_party/stb/stb_truetype_impl.cpp", "third_party/stb/stb_image_impl.cpp" }, objects, database))
            return false;
        return compile(c_config(cfg, { "-D_GNU_SOURCE" }), find_files("third_party/quickjs", ".c"), objects, database);
    }

    Config with_audio_link(Config cfg)
    {
        cfg.link_flags.insert(cfg.link_flags.end(), { "-ldl", "-lpthread", "-lm" });
        return cfg;
    }

    // The app's flags: ImGui and Vulkan headers, app/ and the shaders slangc writes to build/<mode>/gen.
    Config app_config(const Config& cfg)
    {
        Config app = cfg;
        app.compile_flags.insert(app.compile_flags.end(), { "-Wno-missing-field-initializers", "-isystem", "third_party/imgui", "-isystem", "third_party/vulkan", "-Iapp", std::format("-I{}", (cfg.out_dir() / "gen").string()) });
        return app;
    }

    // Library objects, tools (tools/<name>/main.cpp -> build/<mode>/<name>) and the single test binary.
    // Without `tools`, only the library and the tests are built (CI's sanitizer job runs nothing else).
    bool build_all(const Config& cfg, bool tools = true)
    {
        if (!mkdir_p(cfg.obj_dir()))
            return false;

        std::vector<CompileEntry> database;

        std::vector<fs::path> lib_objects;
        if (!compile(cfg, core_sources(cfg), lib_objects, database))
            return false;
        if (!compile_vendored_core(cfg, lib_objects, database))
            return false;

        std::vector<fs::path> audio_objects;
        if (!compile_audio(cfg, audio_objects, database))
            return false;

        for (const auto& main_cpp : tools ? find_files("tools", ".cpp") : std::vector<fs::path> { })
        {
            const bool audio = uses_audio(main_cpp);
            std::vector<fs::path> objects = lib_objects;
            if (audio)
                objects.insert(objects.end(), audio_objects.begin(), audio_objects.end());
            if (!compile(cfg, { main_cpp }, objects, database))
                return false;
            if (!link(audio ? with_audio_link(cfg) : cfg, cfg.out_dir() / main_cpp.parent_path().filename(), objects))
                return false;
        }

        // All of tests/**/*.cpp (the doctest runner plus every *.test.cpp) links into one binary, together
        // with the audio module.
        std::vector<fs::path> test_objects = lib_objects;
        test_objects.insert(test_objects.end(), audio_objects.begin(), audio_objects.end());
        if (!compile(cfg, find_files("tests", ".cpp"), test_objects, database))
            return false;
        if (!link(with_audio_link(cfg), cfg.test_binary(), test_objects))
            return false;

        // The app's sources too, as build_app compiles them, so editors find their headers.
        const Config app = app_config(cfg);
        for (const auto& source : find_files("app", ".cpp"))
            database.push_back({ source, compile_cmd(app, source, object_path(app, source)).args });
        write_compile_commands(database);
        return true;
    }

    // The graphical viewer in app/. Not part of `build` so the library and tests never need GLFW or Vulkan.
    // Shaders (app/shaders/NAME.slang, entry points vertex_main and fragment_main) are compiled with slangc into
    // NAME.vert.inc and NAME.frag.inc, C arrays of SPIR-V words included by the sources. Files without an entry
    // point are modules the others import.
    bool build_app(const Config& cfg, fs::path& binary)
    {
        if (!mkdir_p(cfg.obj_dir()))
            return false;
        const fs::path gen_dir = cfg.out_dir() / "gen";
        if (!mkdir_p(gen_dir))
            return false;

        // Stages compile on their own: a program may share another's vertex stage. Any shader file changing
        // rebuilds them all, as a module may have changed.
        const std::vector<fs::path> shaders = find_files("app/shaders", ".slang");
        for (const auto& source : shaders)
        {
            std::ifstream in(source);
            const std::string text { std::istreambuf_iterator<char>(in), { } };
            for (const auto [stage, ext] : { std::pair { "vertex", "vert" }, std::pair { "fragment", "frag" } })
            {
                if (text.find(std::format("[shader(\"{}\")]", stage)) == std::string::npos)
                    continue;
                const fs::path output = gen_dir / std::format("{}.{}.inc", source.stem().string(), ext);
                if (!needs_rebuild(output, shaders))
                    continue;
                const fs::path spirv = gen_dir / std::format("{}.{}.spv", source.stem().string(), ext);
                Cmd cmd;
                cmd << "slangc" << source.string() << "-I" << "app/shaders" << "-entry" << std::format("{}_main", stage) << "-stage" << stage << "-target" << "spirv" << "-profile" << "spirv_1_6" << "-O2" << "-o" << spirv.string();
                if (!run(cmd))
                    return false;
                std::ifstream code(spirv, std::ios::binary);
                const std::string bytes { std::istreambuf_iterator<char>(code), { } };
                std::string words = "{";
                for (std::size_t i = 0; i + 4 <= bytes.size(); i += 4)
                {
                    std::uint32_t word = 0;
                    std::memcpy(&word, bytes.data() + i, 4);
                    words += std::format("{}0x{:08x}", i == 0 ? "" : ",", word);
                }
                std::ofstream(output) << words << "}\n";
            }
        }

        std::vector<CompileEntry> ignored;

        // Dear ImGui is vendored as-is; its warnings are not ours.
        Config third_party = cfg;
        third_party.compile_flags.insert(third_party.compile_flags.end(), { "-w", "-isystem", "third_party/imgui", "-isystem", "third_party/vulkan" });
        std::vector<fs::path> imgui_sources = find_files("third_party/imgui", ".cpp");
        std::vector<fs::path> objects;
        if (!compile(third_party, imgui_sources, objects, ignored))
            return false;

        const Config app = app_config(cfg);
        std::vector<fs::path> lib_sources = core_sources(cfg);
        if (!compile(app, lib_sources, objects, ignored))
            return false;
        if (!compile_vendored_core(cfg, objects, ignored))
            return false;
        if (!compile_audio(cfg, objects, ignored))
            return false;
        if (!compile(app, find_files("app", ".cpp"), objects, ignored))
            return false;

        binary = cfg.out_dir() / "rct3pp";
        Config linker = with_audio_link(cfg);
        linker.link_flags.insert(linker.link_flags.end(), { "-lglfw", "-lvulkan" });
        return link(linker, binary, objects);
    }

    // Fuzz targets: fuzz/<name>.cpp -> build/fuzz/<name>, linked with the library objects.
    bool build_fuzz(const Config& cfg, std::vector<fs::path>& targets)
    {
        if (!mkdir_p(cfg.obj_dir()))
            return false;

        std::vector<CompileEntry> database;

        std::vector<fs::path> lib_objects;
        if (!compile(cfg, core_sources(cfg), lib_objects, database))
            return false;
        if (!compile_vendored_core(cfg, lib_objects, database))
            return false;

        std::vector<fs::path> audio_objects;
        for (const auto& source : find_files("fuzz", ".cpp"))
        {
            const bool audio = uses_audio(source);
            if (audio && audio_objects.empty() && !compile_audio(cfg, audio_objects, database))
                return false;
            std::vector<fs::path> objects = lib_objects;
            if (audio)
                objects.insert(objects.end(), audio_objects.begin(), audio_objects.end());
            if (!compile(cfg, { source }, objects, database))
                return false;
            const fs::path binary = cfg.out_dir() / source.stem();
            if (!link(audio ? with_audio_link(cfg) : cfg, binary, objects))
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

    // Runs clang-tidy over our library, tool and fuzz sources using the compile database written by the build.
    // The checks come from .clang-tidy. The files are split across the available cores. CLANGTIDY overrides
    // the binary.
    bool run_tidy(std::size_t jobs)
    {
        std::vector<fs::path> files;
        for (const char* dir : { "src", "tools", "fuzz" })
        {
            const auto found = find_files(dir, ".cpp");
            files.insert(files.end(), found.begin(), found.end());
        }
        if (!fs::exists(kBuildDir / "compile_commands.json"))
        {
            log(Level::Error, "no {}: run ./nob build first", (kBuildDir / "compile_commands.json").string());
            return false;
        }
        const char* binary = std::getenv("CLANGTIDY");
        const std::size_t chunk = (files.size() + jobs - 1) / jobs;
        std::vector<std::future<bool>> pending;
        for (std::size_t begin = 0; begin < files.size(); begin += chunk)
        {
            const std::size_t end = std::min(files.size(), begin + chunk);
            pending.push_back(std::async(std::launch::async, [&files, binary, begin, end] {
                Cmd cmd;
                cmd << (binary ? binary : "clang-tidy") << "-p" << kBuildDir.string() << "--quiet";
                for (std::size_t i = begin; i < end; ++i)
                    cmd << files[i].string();
                return run(cmd);
            }));
        }
        bool ok = true;
        for (auto& f : pending)
            ok = f.get() && ok;
        return ok;
    }

    // Optional assets the project does not track: too big for the repository, or under their own licence.
    // Each is pinned to a release and checked against its SHA-256 after download.
    struct Download
    {
        std::string_view url;
        std::string_view sha256;
        std::string_view file; ///< Under kDownloadDir
    };

    const fs::path kDownloadDir = "downloads";

    constexpr std::array kDownloads = {
        // GNU Unifont, the fallback for characters the game fonts lack. SIL OFL 1.1, or GPL-2.0-or-later with
        // the font embedding exception.
        Download { "https://unifoundry.com/pub/unifont/unifont-15.0.06/font-builds/unifont-15.0.06.ttf", "9282b6eff54eeca2e7f58c9a40a91049bd219f3e6a45fbee8eba013379b9af3a", "unifont-15.0.06.ttf" },
    };

    // Downloads what is missing (needs curl and sha256sum). A file that fails the check is removed.
    struct ScriptRun
    {
        std::string name;
        bool passed = false;
    };

    std::string html_escaped(std::string_view text)
    {
        std::string out;
        for (const char c : text)
        {
            switch (c)
            {
                case '&':
                    out += "&amp;";
                    break;
                case '<':
                    out += "&lt;";
                    break;
                case '>':
                    out += "&gt;";
                    break;
                default:
                    out += c;
            }
        }
        return out;
    }

    /// One page over the scripts run: whether each passed, its log and its screenshots, from the folder each
    /// wrote under `report`.
    bool write_script_report(const fs::path& report, std::span<const ScriptRun> runs)
    {
        std::string body;
        for (const ScriptRun& r : runs)
        {
            std::ifstream in(report / r.name / "log.txt");
            const std::string log_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            body += std::format("<section><h2>{} <span class=\"{}\">{}</span></h2><pre>{}</pre>\n", html_escaped(r.name), r.passed ? "pass" : "fail", r.passed ? "passed" : "failed", html_escaped(log_text));
            for (const fs::path& shot : find_files(report / r.name, ".png"))
            {
                const std::string src = (fs::path(r.name) / shot.filename()).generic_string();
                body += std::format("<figure><a href=\"{0}\"><img src=\"{0}\" loading=\"lazy\"></a><figcaption>{1}</figcaption></figure>\n", html_escaped(src), html_escaped(shot.stem().string()));
            }
            body += "</section>\n";
        }
        std::ofstream out(report / "index.html");
        out << R"(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Script Report</title>
<style>
:root { --bg: #f6f6f4; --fg: #1d1d1b; --muted: #6b6b66; --card: #fff; --pass: #2e7d32; --fail: #c62828; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg: #161615; --fg: #e8e8e4; --muted: #9a9a94; --card: #22221f; --pass: #81c784; --fail: #ef9a9a; } }
:root[data-theme="dark"] { --bg: #161615; --fg: #e8e8e4; --muted: #9a9a94; --card: #22221f; --pass: #81c784; --fail: #ef9a9a; }
body { margin: 0; padding: 16px; background: var(--bg); color: var(--fg); font: 15px/1.5 system-ui, sans-serif; }
main { max-width: 1100px; margin: 0 auto; }
section { background: var(--card); border-radius: 8px; padding: 16px; margin: 16px 0; }
h2 { margin: 0 0 8px; font-size: 18px; }
.pass { color: var(--pass); } .fail { color: var(--fail); }
pre { white-space: pre-wrap; overflow-wrap: anywhere; color: var(--muted); font-size: 13px; }
figure { margin: 12px 0; } img { width: 100%; height: auto; border-radius: 4px; display: block; }
figcaption { color: var(--muted); font-size: 13px; }
</style></head><body><main><h1>Script report</h1>
)" << body << "</main></body></html>\n";
        return static_cast<bool>(out);
    }

    bool run_fetch()
    {
        if (!mkdir_p(kDownloadDir))
            return false;
        for (const Download& d : kDownloads)
        {
            const fs::path target = kDownloadDir / d.file;
            if (fs::exists(target))
            {
                log(Level::Info, "{} is already there", target.string());
                continue;
            }
            const fs::path partial = std::format("{}.part", target.string());
            Cmd get;
            get << "curl" << "-fL" << "--retry" << "2" << "-o" << partial.string() << std::string(d.url);
            if (!run(get))
                return false;
            const fs::path sums = kDownloadDir / "sha256.txt";
            {
                std::ofstream out(sums);
                out << d.sha256 << "  " << partial.string() << "\n";
            }
            Cmd check;
            check << "sha256sum" << "--check" << "--quiet" << sums.string();
            const bool ok = run(check);
            std::error_code ec;
            fs::remove(sums, ec);
            if (!ok)
            {
                log(Level::Error, "{} does not match its checksum", d.url);
                fs::remove(partial, ec);
                return false;
            }
            fs::rename(partial, target, ec);
            if (ec)
                return false;
        }
        return true;
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
    bool tools = true;
    std::vector<std::string> test_args;
    std::string fuzz_target;
    std::string script; ///< For ./nob script
    int fuzz_seconds = 30;
    int jobs = 0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--release")
            release = true;
        else if (arg == "--sanitize")
            sanitize = true;
        else if (arg == "--no-tools")
            tools = false;
        else if (arg == "--seconds" && i + 1 < argc)
            fuzz_seconds = std::atoi(argv[++i]);
        else if ((arg == "-j" || arg == "--jobs") && i + 1 < argc)
            jobs = std::atoi(argv[++i]);
        else if (arg.starts_with("-j") && arg.size() > 2)
            jobs = std::atoi(argv[i] + 2);
        else if (arg == "--filter" && i + 1 < argc)
            test_args.push_back(std::format("-tc=*{}*", argv[++i]));
        else if (arg == "--")
        {
            for (++i; i < argc; ++i)
                test_args.emplace_back(argv[i]);
        }
        else if (arg == "build" || arg == "test" || arg == "clean" || arg == "fuzz" || arg == "app" || arg == "run" || arg == "format" || arg == "tidy" || arg == "fetch" || arg == "script")
            command = arg;
        else if (command == "script" && !arg.starts_with("-"))
            script = arg;
        else if (arg == "--check")
            check = true;
        else if (command == "fuzz" && !arg.starts_with("-"))
            fuzz_target = arg;
        else
        {
            log(Level::Error,
                "unknown argument '{}'. usage: ./nob [build|test|clean|app] [--release] [--sanitize] [--no-tools] [-j N] "
                "[--filter TEXT] [-- TEST_ARGS...] | ./nob fuzz [TARGET] [--seconds N] | ./nob format [--check] | ./nob tidy | ./nob fetch | ./nob script [FILE] [-- APP_ARGS...]",
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

    if (command == "fetch")
        return run_fetch() ? 0 : 1;

    if (command == "fuzz")
    {
        const Config cfg = make_config(false, false, true, job_count(jobs));
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

    Config cfg = make_config(release, sanitize, false, job_count(jobs));
    cfg.database_only = command == "tidy"; // clang-tidy reads the sources itself; it needs only their flags

    if (command == "app" || command == "run" || command == "script")
    {
        fs::path binary;
        if (!build_app(cfg, binary))
            return 1;
        if (command == "app")
            return 0;
        Cmd run_cmd;
        run_cmd << binary.string();
        if (command == "script")
        {
            // Every script under scripts/tests when none is named.
            const std::vector<fs::path> scripts = script.empty() ? find_files("scripts/tests", ".ts") : std::vector<fs::path> { script };
            if (std::ranges::any_of(scripts, [](const fs::path& p) { return p.extension() == ".ts"; }))
            {
                Cmd probe;
                probe << "tsc" << "--version";
                if (!run(probe))
                {
                    log(Level::Error, "TypeScript scripts need tsc on PATH (npm install -g typescript); .js scripts run without it");
                    return 1;
                }
                // TypeScript compiles to build/scripts with the same path under scripts/ (scripts/tsconfig.json).
                Cmd tsc;
                tsc << "tsc" << "-p" << "scripts";
                if (!run(tsc))
                    return 1;
            }
            // Each script's log and screenshots go to build/scripts/report/NAME for review. Each also gets a data
            // folder of its own, build/scripts/data/NAME, made fresh, so no script sees what another saved; it
            // starts as a copy of RCT3PP_DATA when that is set.
            const char* data_env = std::getenv("RCT3PP_DATA");
            const fs::path data_template = data_env ? fs::path(data_env) : fs::path();
            // A park to start custom scenarios from, for folders whose new_scenarios/ is empty: the install's
            // first campaign park, found without regard to case as the game's files are.
            fs::path start_park;
            if (const char* game = std::getenv("RCT3PP_GAME_PATH"); game && fs::is_directory(game))
                for (const fs::directory_entry& entry : fs::recursive_directory_iterator(game, fs::directory_options::skip_permission_denied))
                {
                    std::string name = entry.path().filename().string();
                    std::ranges::transform(name, name.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (name == "vanillahills.dat")
                    {
                        start_park = entry.path();
                        break;
                    }
                }
            std::vector<std::string> failed;
            for (const fs::path& file : scripts)
            {
                const fs::path js = file.extension() == ".ts" ? kBuildDir / "scripts" / fs::relative(file, "scripts").replace_extension(".js") : file;
                const fs::path folder = kBuildDir / "scripts" / "report" / file.stem();
                std::error_code ec;
                fs::remove_all(folder, ec);
                const fs::path data = kBuildDir / "scripts" / "data" / file.stem();
                fs::remove_all(data, ec);
                fs::create_directories(data);
                if (!data_template.empty() && fs::is_directory(data_template))
                    fs::copy(data_template, data, fs::copy_options::recursive);
                if (!start_park.empty() && (!fs::is_directory(data / "new_scenarios") || fs::is_empty(data / "new_scenarios")))
                {
                    fs::create_directories(data / "new_scenarios");
                    fs::copy_file(start_park, data / "new_scenarios" / start_park.filename());
                }
                setenv("RCT3PP_DATA", fs::absolute(data).c_str(), 1);
                Cmd one;
                // Headless: no window pops up, the frames still draw for the screenshots, and no vsync stops them
                // while the screen sleeps. Uncapped, so the script runs as fast as the machine draws.
                one << binary.string() << "--script" << js.string() << "--script-report" << folder.string() << "--headless" << "--max-fps" << "0" << "--seed" << "1";
                one.append(test_args);
                const bool passed = run(one);
                log(passed ? Level::Info : Level::Error, "{}: {}, report in {}", file.stem().string(), passed ? "passed" : "failed", folder.string());
                if (!passed)
                    failed.push_back(file.stem().string());
            }
            log(failed.empty() ? Level::Info : Level::Error, "{} of {} scripts passed", scripts.size() - failed.size(), scripts.size());
            return failed.empty() ? 0 : 1;
        }
        run_cmd.append(test_args);
        return run(run_cmd) ? 0 : 1;
    }

    if (!build_all(cfg, tools))
        return 1;
    if (command == "tidy")
        return run_tidy(job_count(jobs)) ? 0 : 1;
    if (command != "test")
        return 0;

    Cmd cmd;
    cmd << cfg.test_binary().string();
    cmd.append(test_args);
    return run(cmd) ? 0 : 1;
}
