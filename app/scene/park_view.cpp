// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_view.hpp"

#include "logging/log.hpp"
#include "park/camera.hpp"
#include "park/view_lines.hpp"
#include "scene/park_scene.hpp"
#include "simulation/components.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <span>
#include <utility>

namespace rct3::scene
{

    namespace
    {
        constexpr glm::vec4 kGridColour { 0.0f, 0.0f, 0.0f, 0.35f };
        constexpr glm::vec4 kContourColour { 1.0f, 1.0f, 1.0f, 0.45f };
        /// How far from the middle of the view heights are labelled, in world units: farther, they crowd. Our
        /// own value.
        constexpr float kLabelReach = 48.0f;
        constexpr float kLabelLift = 0.6f; ///< Over the path or track, so the label stands above it.
        /// A float younger than this, in seconds, and this near, in world units, takes another amount rather than
        /// a new float beginning. A held brush shows a sum a few times a second, while pieces
        /// laid one after another on neighbouring tiles each show their own.
        constexpr float kMoneyMerge = 0.3f;
        constexpr float kMoneyReach = 1.0f;

        void drop(render::Device& device, ViewLines& lines)
        {
            for (const render::BufferHandle b : { lines.vertices, lines.indices })
            {
                if (b)
                    device.destroy_buffer(b);
            }
            lines = { .stale = lines.stale };
        }

        void update(render::Device& device, ViewLines& lines, bool shown, auto&& make)
        {
            if (!shown)
            {
                drop(device, lines);
                return;
            }
            if (lines.vertices && !lines.stale)
                return;
            set_view_lines(device, lines, make());
        }

        std::u16string whole(float height)
        {
            const std::string text = std::format("{}", std::lround(height));
            return { text.begin(), text.end() };
        }
    } // namespace

    void set_view_lines(render::Device& device, ViewLines& lines, const park::TerrainMesh& mesh)
    {
        drop(device, lines);
        lines.stale = false;
        if (mesh.indices.empty())
            return;
        auto vertices = device.create_buffer({ render::BufferUsage::Vertex, std::as_bytes(std::span(mesh.vertices)), "view line vertices" });
        auto indices = device.create_buffer({ render::BufferUsage::Index, std::as_bytes(std::span(mesh.indices)), "view line indices" });
        if (!vertices || !indices)
        {
            logging::logger("park").warning("view lines: {}", !vertices ? vertices.error() : indices.error());
            if (vertices)
                device.destroy_buffer(*vertices);
            if (indices)
                device.destroy_buffer(*indices);
            return;
        }
        lines.vertices = *vertices;
        lines.indices = *indices;
        lines.count = static_cast<std::uint32_t>(mesh.indices.size());
    }

    void update_view_lines(render::Device& device, ParkScene& scene)
    {
        update(device, scene.grid_lines, scene.show_grid, [&] { return park::grid_lines(scene.grid, kGridColour); });
        update(device, scene.contour_lines, scene.show_contours, [&] { return park::contour_lines(scene.grid, park::kContourStep, kContourColour); });
    }

    void draw_view_lines(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        for (const ViewLines* lines : { &scene.grid_lines, &scene.contour_lines, &scene.land_marks, &scene.delete_marks, &scene.enclosure_marks, &scene.pool_marks })
        {
            if (lines->count == 0)
                continue;
            render::DrawCommand draw;
            draw.pipeline = scene.ground_pipeline;
            draw.vertices = { lines->vertices };
            draw.indices = { lines->indices };
            draw.index_type = render::IndexType::U32;
            draw.first = 0;
            draw.count = lines->count;
            draw.transform = transform;
            draw.texture = scene.blank;
            draw.light = light;
            device.draw(draw);
        }
    }

    void drop_view_lines(render::Device& device, ParkScene& scene)
    {
        drop(device, scene.grid_lines);
        drop(device, scene.contour_lines);
        drop(device, scene.land_marks);
        drop(device, scene.pool_marks);
    }

    glm::vec3 tile_point(const save::TerrainGrid& grid, save::Tile tile)
    {
        const float x = grid.world_x(tile.col) + grid.tile_size_x / 2;
        const float z = grid.world_z(tile.row) + grid.tile_size_z / 2;
        return { x, grid.height_at(x, z).value_or(0), z };
    }

    void float_money(ParkScene& scene, glm::vec3 at, std::int64_t amount, bool zero)
    {
        if (!scene.show_money || (amount == 0 && !zero))
            return;
        const auto near = std::ranges::find_if(scene.money, [&](const FloatingMoney& m) { return m.age < kMoneyMerge && (m.amount > 0) == (amount > 0) && glm::distance(m.at, at) <= kMoneyReach; });
        if (near != scene.money.end())
            near->amount += amount;
        else
            scene.money.push_back({ at, amount, 0 });
    }

    std::vector<ui::MoneyFloat> money_floats(ParkScene& scene, ui::Point size, const ui::Currency& currency, float elapsed)
    {
        // Every attraction's takings are followed, shown or not, so switching them on shows only what comes after.
        for (const auto [e, attraction] : scene.sim.registry.view<const simulation::Attraction>().each())
        {
            std::int64_t& seen = scene.income_seen[e];
            const std::int64_t paid = std::exchange(seen, attraction.income.value);
            const auto& nodes = scene.sim.paths.nodes();
            if (!scene.show_guest_payments || attraction.income.value <= paid || attraction.entrance >= nodes.size())
                continue;
            const simulation::PathNode& n = nodes[attraction.entrance];
            float_money(scene, { scene.grid.world_x(n.tile.col) + scene.grid.tile_size_x / 2, n.height, scene.grid.world_z(n.tile.row) + scene.grid.tile_size_z / 2 }, attraction.income.value - paid);
        }
        for (FloatingMoney& m : scene.money)
            m.age += elapsed;
        std::erase_if(scene.money, [](const FloatingMoney& m) { return m.age >= ui::kMoneyFloatLife; });
        if (size.x <= 0 || size.y <= 0)
            return { };
        const glm::mat4 view = park::view_projection(scene.camera, size.x / size.y);
        std::vector<ui::MoneyFloat> shown;
        const auto show = [&](const FloatingMoney& m) {
            // Off the screen it waits unseen, as a label would.
            const glm::vec4 clip = view * glm::vec4(m.at.x, m.at.y + kLabelLift, m.at.z, 1.0f);
            if (clip.w <= 0)
                return;
            const glm::vec2 ndc = glm::vec2(clip) / clip.w;
            if (std::abs(ndc.x) > 1 || std::abs(ndc.y) > 1)
                return;
            shown.push_back({ { (ndc.x + 1) / 2 * size.x, (1 - ndc.y) / 2 * size.y }, ui::money_text(currency, std::abs(m.amount)), m.amount > 0, m.age });
        };
        if (scene.show_money)
            for (const FloatingMoney& m : scene.money)
                show(m);
        // What letting go of an enclosure drag would cost stands at the pointer's end of it, still, until then.
        if (const EnclosureTool& t = scene.enclosing; t.marked && t.other && t.marked->cost != 0)
            show({ tile_point(scene.grid, *t.other), -t.marked->cost, 0 });
        return shown;
    }

    std::vector<ui::WorldLabel> height_labels(const ParkScene& scene, ui::Point size)
    {
        // The construction panel shows the heights of the ride it builds, the view menu every ride's.
        const std::optional<std::size_t> built = scene.building.heights ? scene.building.ride : std::nullopt;
        if ((!scene.show_path_heights && !scene.show_ride_heights && !built) || size.x <= 0 || size.y <= 0)
            return { };
        const glm::mat4 view = park::view_projection(scene.camera, size.x / size.y);
        const glm::vec3 middle = scene.camera.target;
        std::vector<std::pair<float, ui::WorldLabel>> found;
        const auto add = [&](glm::vec3 at, float height) {
            if (glm::distance(glm::vec2(at.x, at.z), glm::vec2(middle.x, middle.z)) > kLabelReach)
                return;
            const glm::vec4 clip = view * glm::vec4(at.x, at.y + kLabelLift, at.z, 1.0f);
            if (clip.w <= 0)
                return;
            const glm::vec2 ndc = glm::vec2(clip) / clip.w;
            if (std::abs(ndc.x) > 1 || std::abs(ndc.y) > 1)
                return;
            found.push_back({ clip.w, { { (ndc.x + 1) / 2 * size.x, (1 - ndc.y) / 2 * size.y }, whole(height) } });
        };
        const save::TerrainGrid& g = scene.grid;
        if (scene.show_path_heights)
        {
            for (const simulation::PathNode& n : scene.sim.paths.nodes())
            {
                if (!n.removed)
                    add({ g.world_x(n.tile.col) + g.tile_size_x / 2, n.height, g.world_z(n.tile.row) + g.tile_size_z / 2 }, n.height);
            }
        }
        if (scene.show_ride_heights || built)
        {
            for (std::size_t r = 0; r < scene.rides.rides.size(); ++r)
            {
                const save::Ride& ride = scene.rides.rides[r];
                if (!ride.track || ride.base.demolished || (!scene.show_ride_heights && r != built))
                    continue;
                for (const save::TrackPiece& p : ride.track->pieces)
                {
                    const auto height = static_cast<float>(p.height);
                    add({ g.world_x(p.tile_x) + g.tile_size_x / 2, height, g.world_z(p.tile_z) + g.tile_size_z / 2 }, height);
                }
            }
        }
        std::ranges::sort(found, std::greater { }, &std::pair<float, ui::WorldLabel>::first);
        std::vector<ui::WorldLabel> labels;
        labels.reserve(found.size());
        for (auto& [_, label] : found)
            labels.push_back(std::move(label));
        return labels;
    }

} // namespace rct3::scene
