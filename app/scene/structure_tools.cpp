// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/structure_tools.hpp"

#include "logging/log.hpp"
#include "park/camera.hpp"
#include "park/edge_walls.hpp"
#include "park/object/pick.hpp"
#include "park/ride/placement.hpp"
#include "park/tile_shaping.hpp"
#include "parkfile/saved_park.hpp"
#include "parkfile/structure_file.hpp"
#include "scene/ghosts.hpp"
#include "scene/park/footpaths.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/view.hpp"
#include "scene/ride_placing.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/build/delete_area.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr std::string_view kGrabName = "structure_grab";
        constexpr std::string_view kPlaceName = "structure_place";
        constexpr glm::vec4 kMarkColour { 1.0f, 0.85f, 0.25f, 0.9f };

        /// Whether tile `t` is marked, counting the drag going on.
        bool marked(const StructureTools& t, const save::model::TerrainGrid& grid, save::model::Tile tile)
        {
            if (t.corner && t.other && tile.col >= std::min(t.corner->col, t.other->col) && tile.col <= std::max(t.corner->col, t.other->col) && tile.row >= std::min(t.corner->row, t.other->row) && tile.row <= std::max(t.corner->row, t.other->row))
                return !t.unmarking;
            return t.marks[static_cast<std::size_t>(tile.row) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(tile.col)] != 0;
        }

        std::vector<save::model::Tile> marked_tiles(const StructureTools& t, const save::model::TerrainGrid& grid)
        {
            std::vector<save::model::Tile> out;
            if (t.marks.empty())
                return out;
            for (int row = 0; row < grid.height; ++row)
                for (int col = 0; col < grid.width; ++col)
                    if (marked(t, grid, { col, row }))
                        out.push_back({ col, row });
            return out;
        }

        /// What `symbol` costs, as the build lists price it; nothing until they are read or for one they lack.
        /// shortcut: found by its symbol alone; match the overlay too once items of two overlays share a symbol.
        std::int64_t price_of(const park::ParkScene& scene, std::string_view symbol)
        {
            if (!scene.build.items.valid() || scene.build.items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return 0;
            const auto& items = scene.build.items.get();
            const auto item = std::ranges::find(items, symbol, &rct3::park::BuildItem::symbol);
            return item == items.end() ? 0 : item->price;
        }

        std::optional<std::vector<std::byte>> read_bytes(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return std::nullopt;
            std::vector<char> chars { std::istreambuf_iterator<char>(in), { } };
            std::vector<std::byte> bytes(chars.size());
            std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
            return bytes;
        }

        void close_panels(park::ParkScene& scene)
        {
            for (const std::string_view name : { kGrabName, kPlaceName })
                if (auto panel = scene.desktop->root().find(name))
                    panel->close();
        }

    } // namespace

    void stop_structure_tools(park::ParkScene& scene)
    {
        close_panels(scene);
        StructureTools& t = scene.structures;
        const bool shown = !t.marks.empty();
        t.tool = StructureTools::Tool::None;
        t.marks.clear();
        t.corner.reset();
        t.other.reset();
        t.structure.reset();
        t.placed.clear();
        t.ghost_mesh = { };
        t.at.reset();
        t.lift_at.reset();
        t.marks_changed = shown; // The marks go on the next frame
    }

    void toggle_structure_tool(park::ParkScene& scene, StructureTools::Tool tool, const ui::InterfaceAssets& assets)
    {
        StructureTools& t = scene.structures;
        const bool out = t.tool == tool;
        stop_structure_tools(scene);
        if (out)
            return;
        if (tool == StructureTools::Tool::Grab)
        {
            t.tool = tool;
            t.marks.assign(static_cast<std::size_t>(scene.grid.width) * static_cast<std::size_t>(scene.grid.height), 0);
            t.marks_changed = true;
            scene.desktop->root().add<ui::screens::build::DeleteAreaPanel>(std::string(kGrabName), skins::structure_grab_panel_style(), *assets.strings);
        }
        else if (tool == StructureTools::Tool::Place)
            t.asked = ui::screens::dialogs::FileDialogMode::Load;
    }

    bool start_structure_placing(park::ParkScene& scene, const std::filesystem::path& file, const ui::InterfaceAssets& assets)
    {
        const auto bytes = read_bytes(file);
        if (!bytes)
            return false;
        std::optional<save::model::Structure> read;
        if (parkfile::is_park_file(*bytes))
        {
            if (auto ours = parkfile::read_structure_file(*bytes))
                read = std::move(*ours);
            else
                logging::logger("park").warning("structures: {}: {}", file.filename().string(), ours.error());
        }
        else if (auto original = save::model::read_structure(*bytes))
            read = std::move(*original);
        else
            logging::logger("park").warning("structures: {}: {}", file.filename().string(), save::model::to_string(original.error()));
        if (!read)
            return false;
        stop_structure_tools(scene);
        StructureTools& t = scene.structures;
        t.tool = StructureTools::Tool::Place;
        t.structure = std::move(read);
        t.turn = { };
        auto& panel = scene.desktop->root().add<ui::screens::build::DeleteAreaPanel>(std::string(kPlaceName), skins::structure_place_panel_style(), *assets.strings);
        panel.set_marked(true);
        logging::logger("park").info("structures: placing {} ({} items)", t.structure->name, t.structure->pieces.size());
        return true;
    }

    void update_structure_tools(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, bool shift, std::span<const ui::PlayCommand> commands)
    {
        StructureTools& t = scene.structures;
        if (t.tool == StructureTools::Tool::Grab)
        {
            // A drag marks the tiles it spans, or unmarks them when it starts on a marked one.
            const auto tile = !over ? ground_tile(scene, at, size) : std::nullopt;
            if (down && (t.corner || tile))
            {
                if (!t.corner)
                {
                    t.corner = tile;
                    t.unmarking = marked(t, scene.grid, *tile);
                }
                if (tile && tile != t.other)
                {
                    t.other = tile;
                    t.marks_changed = true;
                }
            }
            else if (t.corner && t.other)
            {
                for (const save::model::Tile m : marked_tiles(t, scene.grid))
                    t.marks[static_cast<std::size_t>(m.row) * static_cast<std::size_t>(scene.grid.width) + static_cast<std::size_t>(m.col)] = 2; // Kept through the reset below
                for (std::uint8_t& m : t.marks)
                    m = m == 2 ? 1 : 0;
                t.corner.reset();
                t.other.reset();
            }
            if (auto panel = scene.desktop->root().find_as<ui::screens::build::DeleteAreaPanel>(kGrabName))
            {
                const bool any = std::ranges::contains(t.marks, std::uint8_t { 1 });
                panel->set_marked(any);
                if (const auto asked = panel->take_action(); asked == ui::screens::build::DeleteAreaAction::Clear)
                {
                    std::ranges::fill(t.marks, 0);
                    t.marks_changed = true;
                }
                else if (asked == ui::screens::build::DeleteAreaAction::Delete && any)
                {
                    save::model::Structure s = rct3::park::pick_structure(marked_tiles(t, scene.grid), scene.objects, scene.models, scene.grid);
                    logging::logger("park").info("structures: {} items on {} tiles picked out", s.pieces.size(), std::ranges::count(s.tiles, 1));
                    if (!s.pieces.empty())
                    {
                        t.structure = std::move(s);
                        t.asked = ui::screens::dialogs::FileDialogMode::Save;
                    }
                }
            }
        }
        if (t.marks_changed)
        {
            rct3::park::TerrainMesh mesh;
            for (const save::model::Tile m : marked_tiles(t, scene.grid))
                rct3::park::tile_marks(mesh, scene.grid, m.col, m.row, m.col, m.row, rct3::park::TilePart::Whole, kMarkColour);
            park::set_view_lines(device, scene.delete_marks, mesh);
            t.marks_changed = false;
        }
        if (t.tool != StructureTools::Tool::Place || !t.structure)
            return;

        auto panel = scene.desktop->root().find_as<ui::screens::build::DeleteAreaPanel>(kPlaceName);
        bool turned = panel && panel->take_action() == ui::screens::build::DeleteAreaAction::Clear;
        if (!park::typing(scene))
            for (const ui::PlayCommand c : commands)
            {
                if (c == ui::PlayCommand::TurnItemRight)
                    t.turn = t.turn.turned(1);
                else if (c == ui::PlayCommand::TurnItemLeft)
                    t.turn = t.turn.turned(-1);
            }
        if (turned)
            t.turn = t.turn.turned(1);

        // Raised with Shift it stays on its tiles; otherwise it follows the ground under the pointer.
        const std::optional<rct3::park::Ray> ray = !over && size.x > 0 && size.y > 0 ? std::optional(rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y })) : std::nullopt;
        if (shift && t.at && t.lift_at)
        {
            if (ray)
                if (const auto h = lifted_height(*ray, *t.lift_at, t.lift_height))
                    t.height = std::max(t.ground, *h);
        }
        else if (ray)
        {
            const auto distance = rct3::park::object::ground_hit(*ray, scene.grid);
            const auto tile = ground_tile(scene, at, size);
            t.at = tile;
            t.lift_at.reset();
            if (tile && distance)
            {
                t.ground = rct3::park::ride::ride_site(*tile, { 1, 1 }, { }, scene.grid).height;
                t.height = t.ground;
                if (shift)
                {
                    t.lift_at = ray->origin + ray->direction * *distance;
                    t.lift_height = t.height;
                }
            }
        }
        if (!t.at)
        {
            t.placed.clear();
            t.ghost_mesh = { };
            return;
        }
        if (t.solids_of != scene.objects.size())
        {
            t.solids = rct3::park::path::solid_tiles(scene.objects, scene.models, scene.grid);
            t.solids_of = scene.objects.size();
        }
        std::vector<save::model::PlacedObject> placed = rct3::park::structure_objects(*t.structure, *t.at, t.turn, t.height, scene.models);
        std::erase_if(placed, [&](const save::model::PlacedObject& o) { return !load_object_model(scene, t.models, assets, o); });
        const bool moved = placed.size() != t.placed.size() || !std::ranges::equal(placed, t.placed, [](const save::model::PlacedObject& a, const save::model::PlacedObject& b) { return a.col == b.col && a.row == b.row && a.base_height == b.base_height && a.rotation == b.rotation; });
        t.placed = std::move(placed);
        const rct3::park::StructureProblem problem = rct3::park::structure_problem(*t.structure, *t.at, t.turn, t.placed, scene.models, t.solids, scene.footpaths.paths, scene.grid);
        t.cost = 0;
        for (const save::model::PlacedObject& o : t.placed)
            t.cost += price_of(scene, o.symbol);
        const bool affordable = !scene.park.cash || *scene.park.cash >= units::Money { t.cost };
        if (!moved && problem == t.problem && affordable == t.affordable && !t.ghost_mesh.vertices.empty())
            return;
        t.problem = problem;
        t.affordable = affordable;
        t.ghost_mesh = rct3::park::object::object_mesh(t.placed, scene.models, scene.grid);
        const glm::vec4 tint = problem == rct3::park::StructureProblem::None && affordable ? kGhostTint : kRefusedTint;
        for (rct3::park::GroundVertex& v : t.ghost_mesh.vertices)
            v.colour *= tint;
        ghost_textures(device, scene, t.ghost_mesh);
    }

    bool structure_tools_click(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        StructureTools& t = scene.structures;
        if (t.tool == StructureTools::Tool::Grab)
            return true; // Its drags mark; a click picks nothing
        if (t.tool != StructureTools::Tool::Place || !t.structure)
            return false;
        if (t.placed.empty() || t.problem != rct3::park::StructureProblem::None || !simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, units::Money { t.cost }, simulation::park::LedgerLine::Landscaping, false))
            return true;
        wait_for_build(scene);
        ++scene.built;
        // The tool stays out for another, as in the game.
        const std::size_t first = scene.objects.size();
        std::uint64_t id = last_id(scene);
        for (save::model::PlacedObject o : t.placed)
        {
            o.id = ++id;
            o.owner_ref = o.id;
            scene.placed.insert(o.id);
            scene.objects.push_back(std::move(o));
        }
        objects_changed(scene);
        for (std::size_t i = first; i < scene.objects.size(); ++i)
        {
            if (const auto wall = rct3::park::edge_wall(scene.objects[i], scene.models, scene.grid))
                park::wall_changed(device, scene, assets, *wall, true);
            park::animate_scenery(device, scene, i);
        }
        const save::model::TerrainGrid& g = scene.grid;
        park::float_money(scene, { g.world_x(t.at->col) + g.tile_size_x / 2, static_cast<float>(t.height), g.world_z(t.at->row) + g.tile_size_z / 2 }, -t.cost);
        logging::logger("park").info("structures: built {} ({} items) at {},{} height {} turned {}", t.structure->name, t.placed.size(), t.at->col, t.at->row, t.height, t.turn.value);
        t.ghost_mesh = { };
        return true;
    }

    void draw_structure_ghost(render::Device& device, const park::ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.structures.tool == StructureTools::Tool::Place && !scene.structures.ghost_mesh.vertices.empty())
            draw_ghost_mesh(device, scene, scene.structures.ghost_mesh, transform, light);
    }

} // namespace rct3::scene
