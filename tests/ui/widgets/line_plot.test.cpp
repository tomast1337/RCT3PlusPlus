// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/line_plot.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("samples are stretched over the plot's height, highest at the top")
{
    CHECK(plot_y(10, 0, 10, 50) == 0);
    CHECK(plot_y(0, 0, 10, 50) == 50);
    CHECK(plot_y(5, 0, 10, 50) == 25);
    CHECK(plot_y(-1, -2, 2, 40) == 30);
    // A flat series sits in the middle.
    CHECK(plot_y(3, 3, 3, 40) == 20);
}
