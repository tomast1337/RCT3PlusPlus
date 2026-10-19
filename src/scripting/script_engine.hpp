// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs JavaScript (QuickJS-NG). A script is an ES module run once; it may await, and the engine moves it on a
// frame at a time with tick(). Scripts written in TypeScript are compiled to JavaScript first.
//
// The host gives scripts what they may do as native functions, each under a dotted name ("pointer.move"
// makes the object `pointer` and its function `move`). Values cross as JSON: a native gets its arguments as a
// JSON array and returns JSON (null for undefined). The engine itself only adds `frames(n)`, a promise kept
// after n more ticks, and `log(...)`, which writes its arguments to the log.
//
// Scripts have no files, network or system access: the engine's C library module is not built in. A script
// that runs on for too long in one tick is stopped.
#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace rct3::scripting
{

    using Json = nlohmann::json;
    /// A native function given to scripts. Throwing std::exception throws a JavaScript error in the script.
    using Native = std::function<Json(const Json& arguments)>;

    enum class ScriptState : unsigned char
    {
        Running, ///< Waiting on frames or something else to happen
        Finished,
        Failed,
    };

    class ScriptEngine
    {
    public:
        ScriptEngine();
        ~ScriptEngine();
        ScriptEngine(const ScriptEngine&) = delete;
        ScriptEngine& operator=(const ScriptEngine&) = delete;

        /// Gives scripts `fn` as `name`. Call before run().
        void define(std::string_view name, Native fn);

        /// Runs the plain script `source` (not a module, so it cannot wait) at once; false when it threw, with
        /// error() telling why. For helpers the host writes in JavaScript over its natives.
        bool evaluate(std::string_view source, std::string_view file);

        /// Starts the module `source`, named `file` in errors, and runs it to its first wait.
        void run(std::string_view source, std::string_view file);

        /// One frame on: keeps the waits that are due and runs the script until it waits again.
        ScriptState tick();

        [[nodiscard]] ScriptState state() const;
        /// What made the script fail, with its stack.
        [[nodiscard]] const std::string& error() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace rct3::scripting
