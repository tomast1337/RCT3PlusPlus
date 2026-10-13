// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widget.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("children are kept in drawing order")
{
    Widget root("root");
    Widget& a = root.add<Widget>("a");
    Widget& b = root.add<Widget>("b");
    CHECK(a.parent() == &root);
    REQUIRE(root.children().size() == 2);

    root.raise(a);
    CHECK(root.children()[0].get() == &b);
    CHECK(root.children()[1].get() == &a);
    root.lower(a);
    CHECK(root.children()[0].get() == &a);
    root.raise(a);

    auto detached = root.remove(a);
    REQUIRE(detached);
    CHECK(detached->parent() == nullptr);
    CHECK(root.children().size() == 1);
    CHECK_FALSE(root.remove(*detached));
}

TEST_CASE("find follows a path of names")
{
    Widget root("root");
    Widget& options = root.add<Widget>("options");
    Widget& ok = options.add<Widget>("ok");
    root.add<Widget>("other").add<Widget>("ok");

    CHECK(root.find("options") == &options);
    CHECK(root.find("options/ok") == &ok);
    CHECK_FALSE(root.find("options/cancel"));
    CHECK_FALSE(root.find("ok"));
}
