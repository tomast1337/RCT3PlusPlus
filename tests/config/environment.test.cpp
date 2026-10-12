// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/environment.hpp"

#include <doctest/doctest.h>
#include <set>
#include <string>

using namespace rct3::config;

TEST_CASE("every variable is registered once, in id order, with the project prefix")
{
    const auto vars = env_vars();
    REQUIRE(vars.size() == kEnvVarCount);
    std::set<std::string_view> names;
    for (std::size_t i = 0; i < vars.size(); ++i)
    {
        CHECK(static_cast<std::size_t>(vars[i].var) == i);
        CHECK(vars[i].name.starts_with("RCT3PP_"));
        CHECK_FALSE(vars[i].description.empty());
        CHECK(names.insert(vars[i].name).second);
    }
    CHECK(env_name(EnvVar::GamePath) == "RCT3PP_GAME_PATH");
}

TEST_CASE("values are returned as given and empty values count as unset")
{
    const auto env = Environment::from_values({ { EnvVar::GamePath, "/games/park" }, { EnvVar::Validation, "" } });
    REQUIRE(env.get(EnvVar::GamePath).has_value());
    CHECK(*env.get(EnvVar::GamePath) == "/games/park");
    CHECK(env.path(EnvVar::GamePath) == std::filesystem::path("/games/park"));
    CHECK_FALSE(env.get(EnvVar::Validation).has_value());
    CHECK_FALSE(Environment().get(EnvVar::GamePath).has_value());
    CHECK_FALSE(Environment().path(EnvVar::GamePath).has_value());
}

TEST_CASE("flags are on for any value except the usual spellings of off")
{
    auto flag = [](std::string_view value) { return Environment::from_values({ { EnvVar::Validation, value } }).flag(EnvVar::Validation); };
    CHECK(flag("1"));
    CHECK(flag("yes"));
    CHECK(flag("anything"));
    CHECK_FALSE(flag("0"));
    CHECK_FALSE(flag("false"));
    CHECK_FALSE(flag("No"));
    CHECK_FALSE(flag("OFF"));
    CHECK_FALSE(flag(""));
    CHECK_FALSE(Environment().flag(EnvVar::Validation));
}
