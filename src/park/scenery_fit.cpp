// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/scenery_fit.hpp"

#include "core/types.hpp"
#include "park/item_category.hpp"
#include "park/object/placement.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace rct3::park
{

    path::SolidTiles scenery_room(std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models, const save::model::TerrainGrid& grid, FitRules rules)
    {
        if (rules == FitRules::Park)
            return path::solid_tiles(objects, models, grid);
        std::vector<save::model::PlacedObject> props;
        for (const save::model::PlacedObject& o : objects)
            if (const auto model = models.find(object::model_ref(o)); model != models.end() && is_prop(model->second.category))
                props.push_back(o);
        return path::solid_tiles(props, models, grid);
    }

    ride::SiteProblem scenery_problem(std::span<const save::model::PlacedObject> placed, const object::ObjectModels& models, const path::SolidTiles& room, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid, FitRules rules)
    {
        for (const save::model::PlacedObject& o : placed)
        {
            const auto model = models.find(object::model_ref(o));
            const std::array<i32, 2> squares = model != models.end() ? model->second.squares : std::array { 1, 1 };
            // The object names its footprint's tile of lowest column and row.
            const bool across = (o.rotation & 1) != 0;
            const bool on_map = grid.contains(o.col, o.row) && grid.contains(o.col + squares[across ? 1 : 0] - 1, o.row + squares[across ? 0 : 1] - 1);
            if (rules == FitRules::Park ? !object::on_owned_land(o, squares, grid) : !on_map)
                return ride::SiteProblem::NotOwned;
        }
        const path::Clash c = path::clash(path::solid_tiles(placed, models, grid), room, rules == FitRules::Park ? paths : std::span<const save::model::PathTile> { }, grid);
        return c == path::Clash::None ? ride::SiteProblem::None : c == path::Clash::Path ? ride::SiteProblem::Path : ride::SiteProblem::Blocked;
    }

} // namespace rct3::park
