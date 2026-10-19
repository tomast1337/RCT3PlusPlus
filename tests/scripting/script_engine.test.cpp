// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/script_engine.hpp"

#include <doctest/doctest.h>

using namespace rct3::scripting;

TEST_CASE("a script calls natives with JSON and gets JSON back")
{
    ScriptEngine engine;
    Json seen;
    engine.define("park.add", [](const Json& a) { return Json(a[0].get<double>() + a[1].get<double>()); });
    engine.define("report", [&](const Json& a) {
        seen = a[0];
        return Json(nullptr);
    });
    engine.run("report({ sum: park.add(2, 3), tag: 'x' });", "sum.js");
    CHECK(engine.state() == ScriptState::Finished);
    CHECK(seen == Json { { "sum", 5 }, { "tag", "x" } });
}

TEST_CASE("helpers evaluated first are there for the script")
{
    ScriptEngine engine;
    Json seen;
    engine.define("report", [&](const Json& a) {
        seen = a[0];
        return Json(nullptr);
    });
    CHECK(engine.evaluate("globalThis.twice = (n) => 2 * n;", "helpers.js"));
    engine.run("report(twice(4));", "use.js");
    CHECK(seen == 8);
    CHECK_FALSE(engine.evaluate("nope(", "bad.js"));
}

TEST_CASE("a script waits the frames it asks for")
{
    ScriptEngine engine;
    int reached = 0;
    engine.define("reach", [&](const Json& a) {
        reached = a[0].get<int>();
        return Json(nullptr);
    });
    engine.run("reach(1); await frames(2); reach(2);", "wait.js");
    CHECK(reached == 1);
    CHECK(engine.tick() == ScriptState::Running);
    CHECK(reached == 1);
    CHECK(engine.tick() == ScriptState::Finished);
    CHECK(reached == 2);
}

TEST_CASE("errors in scripts and natives fail the script with their message")
{
    ScriptEngine thrown;
    thrown.run("await frames(1); throw new Error('broken');", "throw.js");
    CHECK(thrown.tick() == ScriptState::Failed);
    CHECK(thrown.error().find("broken") != std::string::npos);

    ScriptEngine native;
    native.define("bad", [](const Json&) -> Json { throw std::runtime_error("no park"); });
    native.run("bad();", "native.js");
    CHECK(native.state() == ScriptState::Failed);
    CHECK(native.error().find("no park") != std::string::npos);

    ScriptEngine syntax;
    syntax.run("let = ;", "syntax.js");
    CHECK(syntax.state() == ScriptState::Failed);
}

TEST_CASE("a script that never yields is stopped")
{
    ScriptEngine engine;
    engine.run("for (;;) {}", "loop.js");
    CHECK(engine.state() == ScriptState::Failed);
}
