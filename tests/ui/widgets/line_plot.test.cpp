// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/line_plot.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::ui;

TEST_CASE("a scale's full value is the round number just above the largest sample")
{
    const auto full = [](std::vector<float> v) { return full_scale(v); };
    CHECK(full({ 3, 7 }) == doctest::Approx(10));
    CHECK(full({ 120, 180 }) == doctest::Approx(200));
    CHECK(full({ 0.3f, 0.41f }) == doctest::Approx(0.5f));
    CHECK(full({ 600 }) == doctest::Approx(1000));
    // Either side of zero, and a round number is its own full value.
    CHECK(full({ -4500, 900 }) == doctest::Approx(5000));
    CHECK(full({ 2000 }) == doctest::Approx(2000));
    // Nothing to go by.
    CHECK(full({ }) == 1);
    CHECK(full({ 0, 0 }) == 1);
}

TEST_CASE("samples sit up the plot from zero at the bottom, or from the middle on a centred scale")
{
    CHECK(plot_y(0, 10, false, 50) == 50);
    CHECK(plot_y(10, 10, false, 50) == 0);
    CHECK(plot_y(5, 10, false, 50) == 25);
    CHECK(plot_y(0, 10, true, 40) == 20);
    CHECK(plot_y(-10, 10, true, 40) == 40);
    CHECK(plot_y(5, 10, true, 40) == 10);
    // Off the scale, kept at its edge.
    CHECK(plot_y(-1, 10, false, 50) == 50);
}

TEST_CASE("the numbers up a scale go from its full value down to zero, or to minus it when centred")
{
    CHECK(axis_values(200, false) == std::array<float, 5> { 200, 150, 100, 50, 0 });
    CHECK(axis_values(10, true) == std::array<float, 5> { 10, 5, 0, -5, -10 });
}

TEST_CASE("the newest sample is at the right edge, older ones a slot apart to its left")
{
    CHECK(plot_x(0, 5, 100) == 100);
    CHECK(plot_x(4, 5, 100) == 0);
    CHECK(plot_x(1, 5, 100) == 75);
    CHECK(plot_x(0, 1, 100) == 100);
}

TEST_CASE("a plot spans its slots, or its longest series")
{
    LinePlot plot("plot");
    plot.series = { { .values = { 1, 2, 3 } }, { .values = { 1 } } };
    CHECK(plot.span() == 3);
    plot.slots = 64;
    CHECK(plot.span() == 64);
}
