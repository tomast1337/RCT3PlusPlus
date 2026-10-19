// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/script_host.hpp"

#include "logging/log.hpp"
#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "scene/park_scene.hpp"

#include <fstream>
#include <sstream>

namespace rct3::scene
{

    namespace
    {
        using scripting::Json;

        // The helpers scripts get that only wait on the natives below.
        constexpr std::string_view kHelpers = R"js(
pointer.drag = async (from, to, count = 10) => {
    pointer.move(from[0], from[1]);
    await frames(1);
    pointer.press();
    for (let i = 1; i <= count; ++i) {
        await frames(1);
        pointer.move(from[0] + (to[0] - from[0]) * i / count, from[1] + (to[1] - from[1]) * i / count);
    }
    await frames(1);
    pointer.release();
    await frames(1);
};
ui.click = async (path) => {
    const at = ui.find(path);
    if (!at) throw new Error(`no widget ${path} is shown`);
    pointer.move(at.x, at.y);
    await frames(1);
    pointer.press();
    await frames(1);
    pointer.release();
    await frames(1);
};
{
    // Read from the screen as the frame ends and written a frame later.
    const take = screenshot;
    globalThis.screenshot = async () => {
        take();
        await frames(3);
    };
}
{
    // The park asked for loads as the frame ends.
    const open = park.open;
    park.open = async (path) => {
        open(path);
        await frames(1);
        await park.ready();
    };
}
park.ready = async () => {
    while (!park.loaded()) await frames(1);
};
)js";

        float number(const Json& arguments, std::size_t i)
        {
            if (i >= arguments.size() || !arguments[i].is_number())
                throw std::invalid_argument("argument " + std::to_string(i + 1) + " must be a number");
            return arguments[i].get<float>();
        }

        const save::TerrainGrid& grid_of(const ParkScene* park)
        {
            if (park == nullptr)
                throw std::runtime_error("no park is open");
            return park->grid;
        }
    } // namespace

    std::unique_ptr<ScriptHost> ScriptHost::start(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
        {
            logging::logger("script").error("{}: cannot read", file.string());
            return nullptr;
        }
        std::ostringstream text;
        text << in.rdbuf();
        std::unique_ptr<ScriptHost> host(new ScriptHost());
        host->define();
        if (!host->engine_.evaluate(kHelpers, "rct3pp:helpers"))
        {
            logging::logger("script").error("helpers: {}", host->engine_.error());
            return nullptr;
        }
        host->engine_.run(text.str(), file.filename().string());
        return host;
    }

    void ScriptHost::define()
    {
        engine_.define("pointer.move", [this](const Json& a) {
            at_ = glm::vec2 { number(a, 0), number(a, 1) };
            return Json(nullptr);
        });
        engine_.define("pointer.press", [this](const Json&) {
            down_ = true;
            return Json(nullptr);
        });
        engine_.define("pointer.release", [this](const Json&) {
            down_ = false;
            return Json(nullptr);
        });
        engine_.define("ui.find", [this](const Json& a) -> Json {
            if (a.empty() || !a[0].is_string())
                throw std::invalid_argument("ui.find takes a widget path");
            if (park_ == nullptr || device_ == nullptr)
                return nullptr;
            const ui::Widget* w = park_->desktop->root().find(a[0].get<std::string>());
            const render::Extent view = device_->extent();
            if (w == nullptr || !w->visible || view.width == 0 || view.height == 0)
                return nullptr;
            const float scale = park_interface_scale(*device_, *park_);
            const ui::Rect& r = w->rect();
            return { { "x", (r.x0 + r.x1) / 2 * scale / static_cast<float>(view.width) }, { "y", (r.y0 + r.y1) / 2 * scale / static_cast<float>(view.height) } };
        });
        engine_.define("screenshot", [this](const Json&) {
            screenshot_ = true;
            return Json(nullptr);
        });
        engine_.define("quit", [this](const Json& a) {
            quit_ = a.empty() || !a[0].is_number() ? 0 : a[0].get<int>();
            return Json(nullptr);
        });
        engine_.define("park.open", [this](const Json& a) {
            if (a.empty() || !a[0].is_string())
                throw std::invalid_argument("park.open takes a path in the game's folder");
            open_ = a[0].get<std::string>();
            return Json(nullptr);
        });
        engine_.define("park.loaded", [this](const Json&) { return Json(park_ != nullptr); });
        engine_.define("park.cash", [this](const Json&) -> Json {
            if (park_ == nullptr)
                throw std::runtime_error("no park is open");
            return park_->park.cash ? Json(*park_->park.cash) : Json(nullptr);
        });
        engine_.define("map.size", [this](const Json&) {
            const save::TerrainGrid& g = grid_of(park_);
            return Json { { "width", g.width }, { "height", g.height } };
        });
        engine_.define("map.tile", [this](const Json& a) -> Json {
            const save::TerrainGrid& g = grid_of(park_);
            const auto col = static_cast<int>(number(a, 0));
            const auto row = static_cast<int>(number(a, 1));
            if (!g.contains(col, row))
                return nullptr;
            const save::TerrainCell& c = g.at(col, row);
            return { { "corners", c.corners }, { "surface", c.surface }, { "owned", park::owned_outright(c) } };
        });
        engine_.define("map.screen", [this](const Json& a) -> Json {
            const save::TerrainGrid& g = grid_of(park_);
            const auto col = static_cast<int>(number(a, 0));
            const auto row = static_cast<int>(number(a, 1));
            const render::Extent view = device_->extent();
            if (!g.contains(col, row) || view.width == 0 || view.height == 0)
                return nullptr;
            const float x = g.world_x(col) + g.tile_size_x / 2;
            const float z = g.world_z(row) + g.tile_size_z / 2;
            const glm::vec4 clip = park::view_projection(park_->camera, static_cast<float>(view.width) / static_cast<float>(view.height)) * glm::vec4(x, g.height_at(x, z).value_or(0), z, 1);
            if (clip.w <= 0)
                return nullptr;
            const glm::vec2 share { (clip.x / clip.w + 1) / 2, (1 - clip.y / clip.w) / 2 };
            if (share.x < 0 || share.x > 1 || share.y < 0 || share.y > 1)
                return nullptr;
            return { { "x", share.x }, { "y", share.y } };
        });
        engine_.define("map.water", [this](const Json&) {
            grid_of(park_);
            Json lakes = Json::array();
            for (const save::WaterBody& b : park_->footpaths.water.bodies)
                lakes.push_back({ { "level", b.level }, { "triangles", b.tris.size() } });
            return lakes;
        });
    }

    void ScriptHost::tick(const render::Device& device, ParkScene* park)
    {
        if (quit_)
            return;
        device_ = &device;
        park_ = park;
        const scripting::ScriptState state = engine_.tick();
        device_ = nullptr;
        if (state == scripting::ScriptState::Failed)
        {
            fail(engine_.error());
        }
        else if (state == scripting::ScriptState::Finished && !quit_)
            quit_ = 0;
    }

    std::optional<ScriptHost::Pointer> ScriptHost::pointer(const render::Device& device) const
    {
        if (!at_)
            return std::nullopt;
        const render::Extent view = device.extent();
        return Pointer { at_->x * static_cast<float>(view.width), at_->y * static_cast<float>(view.height), down_ };
    }

    void ScriptHost::fail(const std::string& why)
    {
        logging::logger("script").error("{}", why);
        quit_ = 1;
    }

    std::optional<std::string> ScriptHost::take_park()
    {
        return std::exchange(open_, std::nullopt);
    }

    bool ScriptHost::take_screenshot()
    {
        return std::exchange(screenshot_, false);
    }

} // namespace rct3::scene
