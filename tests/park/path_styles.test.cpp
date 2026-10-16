// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_styles.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("a path type and a queue type become styles with their first item's price")
{
    ovl::PathType path;
    path.name = "Gravel";
    path.name_text = "Gravel_Name";
    path.icon = "Gravel_Icon";
    path.items.resize(2);
    path.items[0].cost = 900;
    path.items[1].cost = 50;
    const PathStyle p = path_style(path);
    CHECK(p.name == "Gravel");
    CHECK(p.name_text == "Gravel_Name");
    CHECK(p.icon == "Gravel_Icon");
    CHECK(p.piece_cost == 900);
    CHECK_FALSE(p.queue);

    ovl::QueueType queue;
    queue.name = "Ropes";
    const PathStyle q = queue_style(queue);
    CHECK(q.name == "Ropes");
    CHECK(q.piece_cost == 0);
    CHECK(q.queue);
}

TEST_CASE("style overlays are told by their ending, whatever its case")
{
    CHECK(is_style_overlay("Gravel_Stub.common.ovl"));
    CHECK(is_style_overlay("gravel_STUB.Common.OVL"));
    CHECK_FALSE(is_style_overlay("_Stub.common.ovl"));
    CHECK_FALSE(is_style_overlay("Gravel_Stub.unique.ovl"));
    CHECK_FALSE(is_style_overlay("Gravel_Flat.common.ovl"));
}
