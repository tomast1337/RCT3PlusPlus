// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/script_host.hpp"

#include "assets/embedded_files.hpp"
#include "logging/log.hpp"
#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "park/water_fill.hpp"
#include "park/waterfall_mesh.hpp"
#include "platform/window.hpp"
#include "random/generator.hpp"
#include "scene/enclosure_tools.hpp"
#include "scene/front_end_scene.hpp"
#include "scene/park_refresh.hpp"
#include "scene/park_scene.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/park_messages.hpp"
#include "simulation/systems.hpp"
#include "strings/unicode.hpp"
#include "ui/key_combo.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/tickbox.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <future>
#include <map>
#include <span>
#include <sstream>
#include <variant>

namespace rct3::scene
{

    namespace
    {
        using scripting::Json;

        // The helpers scripts get that only wait on the natives below, in assets/scripting/helpers.js.
        constexpr std::string_view kHelpersPath = "scripting/helpers.js";

        float number(const Json& arguments, std::size_t i)
        {
            if (i >= arguments.size() || !arguments[i].is_number())
                throw std::invalid_argument(std::format("argument {} must be a number", i + 1));
            return arguments[i].get<float>();
        }

        /// A script's map point, { col, row, up }: a place in tiles (fractions between them) and how far above
        /// the ground there, in world units.
        glm::vec3 world_point(const save::TerrainGrid& g, const Json& p)
        {
            if (!p.is_object() || !p.contains("col") || !p.contains("row") || !p["col"].is_number() || !p["row"].is_number())
                throw std::invalid_argument("a map point is { col, row, up }");
            const float x = g.origin_x + p["col"].get<float>() * g.tile_size_x;
            const float z = g.origin_z + p["row"].get<float>() * g.tile_size_z;
            const float up = p.contains("up") && p["up"].is_number() ? p["up"].get<float>() : 0;
            return { x, g.height_at(x, z).value_or(0) + up, z };
        }

        const save::TerrainGrid& grid_of(const ParkScene* park)
        {
            if (park == nullptr)
                throw std::runtime_error("no park is open");
            return park->grid;
        }

        ParkScene& park_of(ParkScene* park)
        {
            if (park == nullptr)
                throw std::runtime_error("no park is open");
            return *park;
        }

        /// The install's flat rides, once read; none until then.
        std::optional<std::span<const park::BuildItem>> build_items(const ParkScene& park)
        {
            const auto& items = park.build.items;
            if (!items.valid() || items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return std::nullopt;
            return std::span(items.get());
        }

        Json spot_json(const std::optional<save::PlacementSpot>& spot)
        {
            return spot ? Json { { "col", spot->col }, { "row", spot->row }, { "direction", spot->direction } } : Json(nullptr);
        }
    } // namespace

    std::unique_ptr<ScriptHost> ScriptHost::start(const std::filesystem::path& file, std::optional<std::filesystem::path> report, platform::Window& window)
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
        host->window_ = &window;
        if (report)
        {
            std::error_code ec;
            std::filesystem::create_directories(*report, ec);
            host->report_log_.open(*report / "log.txt");
            if (!host->report_log_)
            {
                logging::logger("script").error("{}: cannot write the report there", report->string());
                return nullptr;
            }
            host->report_ = std::move(report);
        }
        host->define();
        const auto helpers = assets::embedded_file(kHelpersPath);
        if (!helpers || !host->engine_.evaluate(assets::as_text(*helpers), "rct3pp:helpers"))
        {
            logging::logger("script").error("helpers: {}", helpers ? host->engine_.error() : std::string("not built in"));
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
        engine_.define("pointer.wheel", [this](const Json& a) {
            wheel_ = number(a, 0);
            return Json(nullptr);
        });
        engine_.define("pointer.press", [this](const Json& a) {
            (!a.empty() && a[0] == "right" ? right_ : down_) = true;
            return Json(nullptr);
        });
        engine_.define("pointer.release", [this](const Json& a) {
            (!a.empty() && a[0] == "right" ? right_ : down_) = false;
            return Json(nullptr);
        });
        engine_.define("pointer.button", [this](const Json& a) {
            if (a.size() < 2 || !a[0].is_string() || !a[1].is_boolean() || (a[0] != "right" && a[0] != "middle"))
                throw std::invalid_argument("pointer.button takes \"right\" or \"middle\" and whether it is held");
            (a[0] == "right" ? right_ : middle_) = a[1].get<bool>();
            return Json(nullptr);
        });
        engine_.define("pointer.shift", [this](const Json& a) {
            shift_ = !a.empty() && a[0].is_boolean() && a[0].get<bool>();
            return Json(nullptr);
        });
        engine_.define("pointer.control", [this](const Json& a) {
            control_ = !a.empty() && a[0].is_boolean() && a[0].get<bool>();
            return Json(nullptr);
        });
        engine_.define("keys.press", [this](const Json& a) {
            const std::string written = !a.empty() && a[0].is_string() ? a[0].get<std::string>() : std::string { };
            auto combo = ui::parse_play_key_combo(written);
            if (!combo)
                combo = ui::parse_key_combo(written);
            if (!combo)
                throw std::invalid_argument("keys.press takes keys such as \"Ctrl+V\"");
            // A letter with Control types nothing and comes as a shortcut, as from the window.
            const ui::Key key = combo->key == ui::Key::Letter && combo->control ? ui::Key::Shortcut : combo->key;
            keys_.push_back({ key, combo->shift, combo->control, combo->character, { }, combo->alt });
            return Json(nullptr);
        });
        engine_.define("keys.type", [this](const Json& a) {
            if (a.empty() || !a[0].is_string())
                throw std::invalid_argument("keys.type takes text");
            const std::u16string text = strings::to_utf16(a[0].get<std::string>());
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                char32_t c = text[i];
                if (strings::is_high_surrogate(c) && i + 1 < text.size())
                    c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
                keys_.push_back({ ui::Key::Character, false, false, c });
            }
            return Json(nullptr);
        });
        engine_.define("clipboard.get", [this](const Json&) -> Json { return strings::to_utf8(window_->clipboard()); });
        engine_.define("clipboard.set", [this](const Json& a) {
            if (a.empty() || !a[0].is_string())
                throw std::invalid_argument("clipboard.set takes text");
            window_->set_clipboard(strings::to_utf16(a[0].get<std::string>()));
            return Json(nullptr);
        });
        engine_.define("ui.find", [this](const Json& a) -> Json {
            if (a.empty() || !a[0].is_string())
                throw std::invalid_argument("ui.find takes a widget path");
            const auto root = shown_root();
            if (!root || device_ == nullptr)
                return nullptr;
            auto w = root->first.find(a[0].get<std::string>());
            const render::Extent view = device_->extent();
            if (!w || !w->visible || view.width == 0 || view.height == 0)
                return nullptr;
            const float scale = root->second;
            const ui::Rect& r = w->rect();
            const float across = scale / static_cast<float>(view.width);
            const float down = scale / static_cast<float>(view.height);
            Json found { { "x", (r.x0 + r.x1) / 2 * across }, { "y", (r.y0 + r.y1) / 2 * down }, { "left", r.x0 * across }, { "top", r.y0 * down }, { "right", r.x1 * across }, { "bottom", r.y1 * down } };
            if (const auto* b = dynamic_cast<const ui::Button*>(as_pointer(w)); b != nullptr && b->toggle)
                found["on"] = b->on;
            if (const auto* t = dynamic_cast<const ui::Tickbox*>(as_pointer(w)); t != nullptr)
                found["on"] = t->ticked;
            if (const auto* e = dynamic_cast<const ui::EditBox*>(as_pointer(w)); e != nullptr)
                found["text"] = strings::to_utf8(e->text);
            if (const auto* l = dynamic_cast<const ui::Label*>(as_pointer(w)); l != nullptr)
                found["text"] = strings::to_utf8(l->text);
            return found;
        });
        engine_.define("ui.children", [this](const Json& a) -> Json {
            if (a.empty() || !a[0].is_string())
                throw std::invalid_argument("ui.children takes a widget path");
            const auto root = shown_root();
            if (!root)
                return nullptr;
            const std::string path = a[0].get<std::string>();
            auto w = path.empty() ? std::optional<ui::Widget&>(root->first) : root->first.find(path);
            if (!w || !w->visible)
                return nullptr;
            Json names = Json::array();
            for (const auto& child : w->children())
                if (child->visible)
                    names.push_back(child->name());
            return names;
        });
        engine_.define("career.start", [this](const Json& a) {
            if (a.empty() || !a[0].is_number_integer() || a[0].get<int>() < 0 || (a.size() > 1 && !a[1].is_number_integer()))
                throw std::invalid_argument("career.start takes a scenario's place in the list and a game");
            career_ = { a.size() > 1 ? a[1].get<int>() : 0, a[0].get<std::size_t>() };
            return Json(nullptr);
        });
        engine_.define("career.map", [this](const Json&) { return Json(park_ == nullptr && front_ != nullptr && front_->live->front->career_board().has_value()); });
        engine_.define("log", [this](const Json& a) {
            std::string line;
            for (const Json& v : a)
                line += std::format("{}{}", line.empty() ? "" : " ", v.is_string() ? v.get<std::string>() : v.dump());
            write(line, false);
            return Json(nullptr);
        });
        engine_.define("screenshot", [this](const Json& a) {
            if (!report_)
                screenshot_ = std::filesystem::path();
            else
            {
                std::string name = !a.empty() && a[0].is_string() ? a[0].get<std::string>() : "shot";
                // Only a plain file name: the script names the picture, not where it goes.
                std::ranges::replace_if(name, [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_'; }, '_');
                screenshot_ = *report_ / std::format("{:02}_{}.png", ++shots_, name);
                write(std::format("screenshot {}", screenshot_->filename().string()), false);
            }
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
        engine_.define("render.live", [this](const Json&) -> Json {
            if (device_ == nullptr)
                throw std::runtime_error("no device");
            const render::FrameStats stats = device_->stats();
            return { { "buffers", stats.buffers }, { "pipelines", stats.pipelines }, { "textures", stats.textures } };
        });
        engine_.define("park.loaded", [this](const Json&) { return Json(park_ != nullptr); });
        engine_.define("park.cash", [this](const Json&) -> Json {
            if (park_ == nullptr)
                throw std::runtime_error("no park is open");
            return park_->park.cash ? Json(*park_->park.cash) : Json(nullptr);
        });
        engine_.define("park.setCash", [this](const Json& a) -> Json {
            if (park_ == nullptr || !park_->park.cash)
                throw std::runtime_error("no park with money is open");
            if (a.size() != 1 || !a[0].is_number_integer())
                throw std::invalid_argument("park.setCash takes hundredths of the currency");
            park_->park.cash = a[0].get<std::int64_t>();
            return Json(nullptr);
        });
        engine_.define("park.setTime", [this](const Json& a) -> Json {
            if (park_ == nullptr)
                throw std::runtime_error("no park is open");
            if (a.size() != 1 || !a[0].is_number())
                throw std::invalid_argument("park.setTime takes a fraction of a day from midnight");
            // Shown at once, without a blend; the clock runs on from there, into the park's hours if outside.
            const float time = a[0].get<float>();
            park_->day.time = park_->day.shown = time;
            park_->day.fade = 0;
            return Json(nullptr);
        });
        engine_.define("park.guests", [this](const Json&) -> Json {
            if (park_ == nullptr)
                throw std::runtime_error("no park is open");
            const auto& registry = park_->sim.registry;
            return Json(std::ranges::distance(registry.view<const simulation::Visitor>(entt::exclude<simulation::Left>)));
        });
        engine_.define("park.dropParties", [this](const Json& a) -> Json {
            if (park_ == nullptr)
                throw std::runtime_error("no park is open");
            // The park's own walkable tiles, not its queues nor the road out to the gate.
            const save::TerrainGrid& g = park_->grid;
            std::vector<simulation::NodeId> spots;
            const auto& nodes = park_->sim.paths.nodes();
            for (simulation::NodeId n = 0; n < nodes.size(); ++n)
                if (const auto& t = nodes[n].tile; !nodes[n].queue && !nodes[n].removed && g.contains(t.col, t.row) && park::owned_outright(g.at(t.col, t.row)))
                    spots.push_back(n);
            if (spots.empty())
                return Json::array();
            auto dice = random::seeded({ static_cast<std::uint64_t>(a.size() > 1 ? number(a, 1) : 1) });
            Json dropped = Json::array();
            for (auto n = static_cast<int>(number(a, 0)); n > 0; --n)
            {
                const auto spot = spots[random::below(dice, spots.size())];
                const auto party = simulation::admit_party(park_->sim, dice, spot);
                dropped.push_back({ { "col", nodes[spot].tile.col }, { "row", nodes[spot].tile.row }, { "guests", party.size() } });
            }
            return dropped;
        });
        engine_.define("park.pathItems", [this](const Json&) -> Json {
            if (park_ == nullptr)
                throw std::runtime_error("no park is open");
            constexpr std::array kKinds { "bin", "bench", "sign", "sight", "photo spot" };
            Json items = Json::array();
            for (const auto [e, item] : park_->sim.registry.view<const simulation::PathItem>().each())
                items.push_back({ { "object", item.object }, { "kind", kKinds.at(std::to_underlying(item.kind)) }, { "broken", item.broken } });
            return items;
        });
        engine_.define("park.breakPathItems", [this](const Json&) -> Json {
            if (park_ == nullptr)
                throw std::runtime_error("no park is open");
            std::size_t broke = 0;
            for (auto [e, item] : park_->sim.registry.view<simulation::PathItem>().each())
                if (!item.broken && item.kind != simulation::PathItemKind::Sight && item.kind != simulation::PathItemKind::PhotoSpot)
                {
                    item.broken = true;
                    ++broke;
                }
            return Json(broke);
        });
        engine_.define("park.addHandyman", [this](const Json& a) -> Json {
            if (park_ == nullptr)
                throw std::runtime_error("no park is open");
            const simulation::PathNetwork& paths = park_->sim.paths;
            const glm::vec3 middle { paths.origin.x + (static_cast<float>(number(a, 0)) + 0.5f) * paths.tile_size, 0, paths.origin.y + (static_cast<float>(number(a, 1)) + 0.5f) * paths.tile_size };
            const auto at = paths.nearest(middle);
            if (!at)
                return Json(false);
            auto& r = park_->sim.registry;
            const entt::entity e = r.create();
            r.emplace<simulation::Staff>(e, simulation::StaffRole::Handyman);
            simulation::start_walking(park_->sim, e, *at);
            return Json(true);
        });
        engine_.define("park.postMessage", [this](const Json& a) -> Json {
            ParkScene& park = park_of(park_);
            if (a.empty() || !a[0].is_string())
                throw std::invalid_argument("park.postMessage takes a text key, and may take a ride's place in rides.list() and a tab number");
            simulation::ParkMessage message { .text_key = a[0].get<std::string>() };
            if (a.size() > 1 && a[1].is_number_unsigned() && a[1].get<std::size_t>() < park.rides.rides.size())
                message.subject = park.rides.rides[a[1].get<std::size_t>()].base.id;
            if (a.size() > 2 && a[2].is_number_integer())
                message.panel = a[2].get<std::int32_t>();
            simulation::post(park.park.messages, std::move(message));
            return Json(nullptr);
        });
        engine_.define("park.messages", [this](const Json&) {
            Json out = Json::array();
            for (const simulation::ParkMessage& m : park_of(park_).park.messages.list)
                out.push_back({ { "key", m.text_key }, { "name", m.subject_name } });
            return out;
        });
        engine_.define("park.date", [this](const Json&) { return Json(park_of(park_).park.time); });
        engine_.define("park.setDate", [this](const Json& a) -> Json {
            if (a.size() != 1 || !a[0].is_number())
                throw std::invalid_argument("park.setDate takes calendar units");
            park_of(park_).park.time = a[0].get<float>();
            return Json(nullptr);
        });
        engine_.define("park.weather", [this](const Json&) { return Json(save::to_string(park_of(park_).park.weather)); });
        engine_.define("park.setWeather", [this](const Json& a) -> Json {
            for (std::int32_t raw = 0; a.size() == 1 && a[0].is_string() && raw <= 6; ++raw)
                if (const save::WeatherType w = save::weather_type_from(raw); a[0].get<std::string>() == save::to_string(w))
                {
                    park_of(park_).park.weather = w;
                    return Json(nullptr);
                }
            throw std::invalid_argument("park.setWeather takes the name of a weather, such as \"light rain\"");
        });
        engine_.define("park.vipGoals", [this](const Json&) {
            Json out = Json::array();
            for (const save::Condition& c : park_of(park_).park_model.objectives.conditions)
                if (c.vip)
                    out.push_back({ { "level", c.difficulty }, { "who", c.vip->who }, { "next", c.vip->next_visit }, { "months", c.vip->months_to_stay }, { "met", c.vip->met }, { "toldMonth", c.vip->warned_month } });
            return out;
        });
        engine_.define("park.vips", [this](const Json&) {
            const ParkScene& park = park_of(park_);
            const auto& r = park.sim.registry;
            Json out = Json::array();
            for (const auto [e, vip, visitor] : r.view<const simulation::Vip, const simulation::Visitor>().each())
            {
                const auto shown = std::ranges::find(park.shown_people, e, &ShownPerson::guest);
                const glm::vec3 at = r.all_of<simulation::Pose>(e) ? r.get<simulation::Pose>(e).position : glm::vec3 { };
                const save::TerrainGrid& g = park.grid;
                out.push_back({ { "name", visitor.name }, { "col", g.tile_size_x > 0 ? (at.x - g.origin_x) / g.tile_size_x : 0 }, { "row", g.tile_size_z > 0 ? (at.z - g.origin_z) / g.tile_size_z : 0 }, { "patience", vip.patience.value }, { "leaving", vip.leaving }, { "gone", r.all_of<simulation::Left>(e) }, { "shown", shown != park.shown_people.end() ? Json(shown - park.shown_people.begin()) : Json(nullptr) } });
            }
            return out;
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
            Json tile { { "corners", c.corners }, { "surface", c.surface }, { "cliff", c.cliff }, { "owned", park::owned_outright(c) }, { "enclosure", park_->enclosures.at(col, row) } };
            for (const save::WaterBody& b : park_->footpaths.water.bodies)
                if (park::holds(b, { col, row }))
                    tile["water"] = b.level;
            return tile;
        });
        engine_.define("map.screen", [this](const Json& a) -> Json {
            const save::TerrainGrid& g = grid_of(park_);
            // A whole tile means its middle; a fraction, that point of it.
            const auto at = [&](std::size_t i) {
                const double v = number(a, i);
                return v == std::floor(v) ? v + 0.5 : v;
            };
            const double col = at(0);
            const double row = at(1);
            const render::Extent view = device_->extent();
            if (!g.contains(static_cast<int>(std::floor(col)), static_cast<int>(std::floor(row))) || view.width == 0 || view.height == 0)
                return nullptr;
            const float x = g.world_x(0) + static_cast<float>(col) * g.tile_size_x;
            const float z = g.world_z(0) + static_cast<float>(row) * g.tile_size_z;
            const glm::vec4 clip = park::view_projection(park_->camera, static_cast<float>(view.width) / static_cast<float>(view.height)) * glm::vec4(x, g.height_at(x, z).value_or(0), z, 1);
            if (clip.w <= 0)
                return nullptr;
            const glm::vec2 share { (clip.x / clip.w + 1) / 2, (1 - clip.y / clip.w) / 2 };
            if (share.x < 0 || share.x > 1 || share.y < 0 || share.y > 1)
                return nullptr;
            return { { "x", share.x }, { "y", share.y } };
        });
        engine_.define("camera.look", [this](const Json& a) {
            const save::TerrainGrid& g = grid_of(park_);
            const glm::vec3 at = world_point(g, a.empty() ? Json() : a[0]);
            if (a.size() < 2 || a[1].is_null())
            {
                // Keeps the way it faces and how far back it is.
                park_->camera.target = at;
                return Json(nullptr);
            }
            park_->camera = park::looking(world_point(g, a[1]), at);
            return Json(nullptr);
        });
        engine_.define("camera.view", [this](const Json&) -> Json {
            const park::ParkCamera& c = park_of(park_).camera;
            const glm::vec3 e = park::eye(c);
            return { { "yaw", c.yaw }, { "pitch", c.pitch }, { "distance", c.distance }, { "target", { c.target.x, c.target.y, c.target.z } }, { "eye", { e.x, e.y, e.z } } };
        });
        engine_.define("options.set", [this](const Json& a) {
            if (front_ == nullptr || a.size() < 2 || !a[0].is_string() || !a[1].is_number())
                throw std::invalid_argument("options.set takes an option's name and a number, with the game running");
            const auto& table = config::game_options_table();
            const auto info = std::ranges::find(table, a[0].get<std::string>(), &config::GameOptionInfo::name);
            if (info == table.end())
                throw std::invalid_argument(std::format("options.set: no option {}", a[0].get<std::string>()));
            front_->live->options.set(info->option, a[1].get<float>());
            return Json(nullptr);
        });
        engine_.define("camera.lens", [this](const Json&) -> Json {
            const auto& drops = park_of(park_).lens.water.drops;
            const auto sheets = std::ranges::count(drops, true, &park::LensDrop::sheet);
            return { { "drops", static_cast<std::int64_t>(drops.size()) - sheets }, { "sheets", sheets } };
        });
        engine_.define("camera.riding", [this](const Json&) -> Json {
            const ParkScene& park = park_of(park_);
            Json out;
            if (park.riding_person)
                out = { { "ride", -1 }, { "car", *park.riding_person }, { "kind", park.person_behind ? "behind" : "guest" } };
            else if (park.riding_flat)
                out = { { "ride", park.riding_flat->first }, { "car", park.riding_flat->second }, { "kind", park.riding_fixed ? "fixed" : "seat" } };
            else if (park.riding && *park.riding < park.car_spots.size())
            {
                constexpr std::array<std::string_view, 3> kEyes { "nose", "front", "rear" };
                out = { { "ride", park.car_spots[*park.riding].ride }, { "car", *park.riding }, { "kind", kEyes[std::to_underlying(park.riding_eye)] } };
            }
            else
                return nullptr;
            out["turn"] = { park.ride_turn.x, park.ride_turn.y };
            if (park.seat)
            {
                const glm::vec3 at((*park.seat)[3]);
                const glm::vec3 ahead((*park.seat)[2]);
                out["eye"] = { at.x, at.y, at.z };
                out["ahead"] = { ahead.x, ahead.y, ahead.z };
            }
            return out;
        });
        engine_.define("map.effects", [this](const Json&) {
            const save::TerrainGrid& g = grid_of(park_);
            Json effects = Json::array();
            for (const auto* list : { &park_->particles.shown, &park_->particles.fired, &park_->particles.sprays })
                for (const ParkParticles::Shown& s : *list)
                {
                    const glm::vec3 p = s.position;
                    effects.push_back({ { "effect", s.effect }, { "col", (p.x - g.origin_x) / g.tile_size_x }, { "row", (p.z - g.origin_z) / g.tile_size_z }, { "up", p.y - g.height_at(p.x, p.z).value_or(0) }, { "live", s.run.particles().count() } });
                }
            return effects;
        });
        engine_.define("map.fire", [this](const Json& a) {
            const save::TerrainGrid& g = grid_of(park_);
            if (a.empty() || !a[0].is_object() || !a[0].contains("effect"))
                return Json(false);
            return Json(fire_park_particles(park_->particles, a[0]["effect"].get<std::string>(), world_point(g, a[0]), glm::vec3(0, 1, 0)));
        });
        engine_.define("map.paths", [this](const Json&) {
            grid_of(park_);
            Json paths = Json::array();
            for (const save::PathTile& p : park_->footpaths.paths)
            {
                // Ground queues keep an elevation too, with the on-ground slope.
                const bool raised = p.elevation && p.elevation->slope != save::kSlopeOnGround;
                paths.push_back({ { "col", p.col }, { "row", p.row }, { "raised", raised }, { "queue", p.kind == save::PathKind::Queue } });
                if (raised)
                    paths.back()["height"] = p.elevation->quantised_height;
            }
            return paths;
        });
        engine_.define("map.supports", [this](const Json&) {
            grid_of(park_);
            std::map<std::pair<int, int>, int> pieces;
            for (const auto& [_, tile] : park_->columns)
                ++pieces[tile];
            Json tiles = Json::array();
            for (const auto& [tile, n] : pieces)
                tiles.push_back({ { "col", tile.first }, { "row", tile.second }, { "pieces", n } });
            return tiles;
        });
        engine_.define("map.objects", [this](const Json&) {
            grid_of(park_);
            Json objects = Json::array();
            for (const save::PlacedObject& o : park_->objects)
            {
                if (!o.hidden)
                    objects.push_back({ { "symbol", o.symbol }, { "overlay", o.overlay }, { "col", o.col }, { "row", o.row } });
            }
            return objects;
        });
        engine_.define("build.flatRides", [this](const Json&) -> Json {
            const auto items = build_items(park_of(park_));
            if (!items)
                return nullptr;
            Json rides = Json::array();
            for (const park::BuildItem& item : *items)
                if (item.kind == park::BuildKind::FlatRide)
                    rides.push_back({ { "symbol", item.symbol }, { "overlay", item.overlay }, { "name", item.name_text }, { "width", item.width }, { "length", item.length }, { "price", item.price }, { "doors", !item.entrance.empty() && !item.exit.empty() }, { "water", item.water_only }, { "fence", item.fence } });
            return rides;
        });
        engine_.define("build.stalls", [this](const Json&) -> Json {
            const auto items = build_items(park_of(park_));
            if (!items)
                return nullptr;
            Json stalls = Json::array();
            for (const park::BuildItem& item : *items)
            {
                if (!item.stall)
                    continue;
                Json goods = Json::array();
                for (const park::BuildItem::Goods& g : item.goods)
                    goods.push_back({ { "item", g.item }, { "price", g.price } });
                stalls.push_back({ { "symbol", item.symbol }, { "overlay", item.overlay }, { "name", item.name_text }, { "width", item.width }, { "length", item.length }, { "price", item.price }, { "goods", std::move(goods) } });
            }
            return stalls;
        });
        engine_.define("stalls.list", [this](const Json&) {
            const ParkScene& park = park_of(park_);
            Json stalls = Json::array();
            for (std::size_t i = 0; i < park.rides.stalls.size(); ++i)
            {
                const save::Stall& stall = park.rides.stalls[i];
                Json items = Json::array();
                for (const save::StallItem& item : stall.items)
                    items.push_back({ { "item", item.kind_symbol }, { "price", item.price }, { "name", item.name }, { "extras", item.extras } });
                Json extras = Json::array();
                for (const save::StallExtra& extra : stall.extras)
                    extras.push_back({ { "extra", extra.kind_symbol }, { "level", extra.level } });
                const bool shown = park.shown && park.shown->kind == park::AttractionRef::Kind::Stall && park.shown->index == i;
                Json one { { "name", stall.base.name }, { "symbol", stall.base.symbol }, { "overlay", stall.base.overlay_path }, { "site", spot_json(stall.base.spot) }, { "open", stall.base.state == save::AttractionState::Open }, { "items", std::move(items) }, { "extras", std::move(extras) }, { "sync", stall.sync }, { "shown", shown }, { "joined", false }, { "demolished", stall.base.demolished } };
                for (const auto [_, a] : park.sim.registry.view<const simulation::Attraction>().each())
                    if (a.saved == stall.base.id)
                    {
                        one["joined"] = true;
                        one["price"] = a.price.value;
                        one["selling"] = a.open;
                    }
                stalls.push_back(std::move(one));
            }
            return stalls;
        });
        engine_.define("build.pick", [this](const Json& a) -> Json {
            if (a.size() < 2 || !a[0].is_string() || !a[1].is_string())
                throw std::invalid_argument("build.pick takes an overlay, a symbol and, for a tracked ride, \"designs\" to offer its saved designs");
            ParkScene& park = park_of(park_);
            const auto items = build_items(park);
            if (!items)
                return false;
            const auto found = std::ranges::find_if(*items, [&](const park::BuildItem& item) { return item.overlay == a[0].get<std::string>() && item.symbol == a[1].get<std::string>(); });
            if (found == items->end())
                return false;
            // A ride built piece by piece needs the interface's assets, which it gets as a pick from a list does.
            if (found->kind == park::BuildKind::TrackedRide)
            {
                park.build.asked = *found;
                park.build.asked_designs = a.size() > 2 && a[2] == "designs";
            }
            // One tool out at a time, as a pick from a list has it.
            if (start_enclosing(park, *found))
            {
                stop_ride_placing(park);
                return true;
            }
            stop_enclosing(park);
            return found->kind == park::BuildKind::TrackedRide || start_ride_placing(park, *found);
        });
        engine_.define("build.construction", [this](const Json&) -> Json {
            const TrackBuilding& b = park_of(park_).building;
            if (!b.ride)
                return nullptr;
            const auto c = b.catalogues.find(b.overlay);
            const bool named = b.picked && c != b.catalogues.end();
            Json designs = Json::array();
            for (const DesignFile& d : b.designs)
                designs.push_back(d.path.filename().string());
            return { { "ride", *b.ride },
                     { "offered", b.candidates.size() },
                     { "piece", named ? Json(c->second.pieces[b.picked->index].record.name) : Json(nullptr) },
                     { "ghost", b.ghost.has_value() },
                     { "arrows", !track_arrows(park_of(park_)).vertices.empty() },
                     { "ghostAt", b.ghost ? Json { { "col", b.ghost->object.col }, { "row", b.ghost->object.row }, { "height", b.ghost->object.base_height.value_or(0) } } : Json(nullptr) },
                     { "clashes", b.clashes },
                     { "completing", b.completing.has_value() },
                     { "proposed", b.proposed.size() },
                     { "complaint", b.complaint },
                     { "testing", b.test.has_value() },
                     { "testAt", b.test ? Json(b.test->train.at) : Json(nullptr) },
                     { "testWrecks", b.test ? std::ranges::count_if(b.test->wrecks, [](const simulation::Wreck& w) { return w.state != simulation::WreckState::Gone; }) : 0 },
                     { "designs", std::move(designs) },
                     { "selected", b.selected ? Json(*b.selected) : Json(nullptr) },
                     { "split", std::to_underlying(b.split) } };
        });
        engine_.define("build.stop", [this](const Json&) {
            stop_ride_placing(park_of(park_));
            stop_enclosing(park_of(park_));
            return Json(nullptr);
        });
        engine_.define("rides.setState", [this](const Json& a) {
            ParkScene& park = park_of(park_);
            constexpr std::array<std::pair<std::string_view, save::AttractionState>, 3> kStates { { { "closed", save::AttractionState::Closed }, { "testing", save::AttractionState::Testing }, { "open", save::AttractionState::Open } } };
            const auto state = a.size() == 2 && a[1].is_string() ? std::ranges::find(kStates, a[1].get<std::string>(), &std::pair<std::string_view, save::AttractionState>::first) : kStates.end();
            if (!a.at(0).is_number_unsigned() || a[0].get<std::size_t>() >= park.rides.rides.size() || state == kStates.end())
                throw std::invalid_argument("rides.setState takes a ride's place in rides.list() and \"closed\", \"testing\" or \"open\"");
            // The reason it stays as it was, as a string key; null once changed.
            const auto refused = change_ride_state(park, a[0].get<std::size_t>(), state->second);
            return refused ? Json(*refused) : Json(nullptr);
        });
        engine_.define("stalls.setState", [this](const Json& a) {
            ParkScene& park = park_of(park_);
            const bool open = a.size() == 2 && a[1] == "open";
            if (!a.at(0).is_number_unsigned() || a[0].get<std::size_t>() >= park.rides.stalls.size() || !(open || (a.size() == 2 && a[1] == "closed")))
                throw std::invalid_argument("stalls.setState takes a stall's place in stalls.list() and \"closed\" or \"open\"");
            const auto refused = change_stall_state(park, a[0].get<std::size_t>(), open ? save::AttractionState::Open : save::AttractionState::Closed);
            return refused ? Json(*refused) : Json(nullptr);
        });
        engine_.define("rides.show", [this](const Json& a) {
            ParkScene& park = park_of(park_);
            if (!a.at(0).is_number_unsigned() || a[0].get<std::size_t>() >= park.rides.rides.size())
                throw std::invalid_argument("rides.show takes a ride's place in rides.list()");
            park.info_requested = park::AttractionRef { park::AttractionRef::Kind::Ride, a[0].get<std::size_t>() };
            return Json(nullptr);
        });
        engine_.define("stalls.show", [this](const Json& a) {
            ParkScene& park = park_of(park_);
            if (!a.at(0).is_number_unsigned() || a[0].get<std::size_t>() >= park.rides.stalls.size())
                throw std::invalid_argument("stalls.show takes a stall's place in stalls.list()");
            park.info_requested = park::AttractionRef { park::AttractionRef::Kind::Stall, a[0].get<std::size_t>() };
            return Json(nullptr);
        });
        engine_.define("people.rename", [this](const Json& a) {
            ParkScene& park = park_of(park_);
            if (!a.at(0).is_number_unsigned() || a[0].get<std::size_t>() >= park.shown_people.size() || !a.at(1).is_string())
                throw std::invalid_argument("people.rename takes a person's place in people.list() and a name");
            park.rename_requested = std::pair { a[0].get<std::size_t>(), strings::to_utf16(a[1].get<std::string>()) };
            return Json(nullptr);
        });
        engine_.define("people.show", [this](const Json& a) {
            ParkScene& park = park_of(park_);
            if (!a.at(0).is_number_unsigned() || a[0].get<std::size_t>() >= park.shown_people.size())
                throw std::invalid_argument("people.show takes a person's place in people.list()");
            park.person_requested = a[0].get<std::size_t>();
            return Json(nullptr);
        });
        engine_.define("people.list", [this](const Json&) {
            const ParkScene& park = park_of(park_);
            Json out = Json::array();
            for (const ShownPerson& p : park.shown_people)
            {
                const save::Person* saved = p.staff ? (p.saved < park.people.staff.size() ? &park.people.staff[p.saved].person : nullptr) : (p.saved < park.people.guests.size() ? &park.people.guests[p.saved].person : nullptr);
                const glm::vec3 standing = standing_at(park, p);
                const save::TerrainGrid& g = park.grid;
                const Json tile = g.tile_size_x > 0 && g.tile_size_z > 0 ? Json { { "col", (standing.x - g.origin_x) / g.tile_size_x }, { "row", (standing.z - g.origin_z) / g.tile_size_z } } : Json(nullptr);
                Json person { { "name", saved != nullptr ? saved->name : std::string { } }, { "staff", p.staff }, { "at", tile } };
                // A seller's post: the object of the stall or ride entrance they serve at.
                if (const auto* work = p.staff && p.saved < park.people.staff.size() ? std::get_if<save::VendorWork>(&park.people.staff[p.saved].work) : nullptr; work != nullptr)
                    person["post"] = work->scenery_item.id;
                // Where the simulation has them, and what of: walking the paths, held by the player, or let go.
                const auto& r = park.sim.registry;
                if (r.valid(p.guest) && !r.all_of<simulation::Left>(p.guest) && r.all_of<simulation::Pose>(p.guest))
                {
                    const glm::vec3 at = r.get<simulation::Pose>(p.guest).position;
                    const auto* errand = r.try_get<simulation::Errand>(p.guest);
                    person["col"] = (at.x - park.grid.origin_x) / park.grid.tile_size_x;
                    person["row"] = (at.z - park.grid.origin_z) / park.grid.tile_size_z;
                    person["up"] = at.y;
                    person["state"] = r.all_of<simulation::Held>(p.guest) ? "held" : r.all_of<simulation::Dropped>(p.guest) ? "dropped" : errand == nullptr ? "still" : errand->kind == simulation::ErrandKind::Riding ? "riding" : errand->kind == simulation::ErrandKind::Queueing ? "queueing" : errand->kind == simulation::ErrandKind::Waiting ? "waiting" : errand->kind == simulation::ErrandKind::Buying ? "buying" : "walking";
                }
                out.push_back(std::move(person));
            }
            return out;
        });
        engine_.define("animals.list", [this](const Json&) {
            const ParkScene& park = park_of(park_);
            const save::TerrainGrid& g = park.grid;
            Json out = Json::array();
            for (const auto [e, animal, pose] : park.sim.registry.view<const simulation::Animal, const simulation::Pose>().each())
            {
                const Json tile { { "col", g.tile_size_x > 0 ? (pose.position.x - g.origin_x) / g.tile_size_x : 0 }, { "row", g.tile_size_z > 0 ? (pose.position.z - g.origin_z) / g.tile_size_z : 0 } };
                out.push_back({ { "id", entt::to_integral(e) }, { "species", animal.species }, { "enclosure", animal.enclosure }, { "crated", animal.job == simulation::AnimalJob::Crated }, { "escaped", animal.escaped }, { "health", animal.health.value }, { "at", tile } });
            }
            return out;
        });
        engine_.define("rides.shown", [this](const Json&) -> Json {
            const ParkScene& park = park_of(park_);
            if (!park.shown || park.shown->kind != park::AttractionRef::Kind::Ride || park.shown->index >= park.rides.rides.size())
                return nullptr;
            const save::Ride& ride = park.rides.rides[park.shown->index];
            for (const ui::AttractionTabInfo& t : ui::attraction_tabs(ui::attraction_kind(ride), ride.base.state))
            {
                if (t.tab == park.info_tab)
                    return Json { { "ride", park.shown->index }, { "tab", t.help } };
            }
            return Json { { "ride", park.shown->index } };
        });
        engine_.define("build.placing", [this](const Json&) -> Json {
            const RidePlacing& p = park_of(park_).placing;
            if (!p.item)
                return nullptr;
            constexpr std::array<std::string_view, 4> kSteps { "ride", "entrance", "exit", "scenery" };
            constexpr std::array<std::string_view, 8> kProblems { "none", "not owned", "path", "blocked", "off water", "under water", "off path", "off enclosure" };
            Json placing { { "step", kSteps.at(static_cast<std::size_t>(p.step)) }, { "problem", kProblems.at(static_cast<std::size_t>(p.problem)) }, { "affordable", p.affordable }, { "ghost", !p.ghost_mesh.vertices.empty() }, { "arrow", !stall_arrow(park_of(park_)).vertices.empty() } };
            if (p.site)
                placing["site"] = { { "col", p.site->col }, { "row", p.site->row }, { "turns", p.site->turns }, { "height", p.site->height } };
            return placing;
        });
        engine_.define("structures.placing", [this](const Json&) -> Json {
            const StructureTools& t = park_of(park_).structures;
            if (t.tool != StructureTools::Tool::Place || !t.structure)
                return nullptr;
            constexpr std::array<std::string_view, 4> kProblems { "none", "not owned", "blocked", "path" };
            Json placing { { "name", t.structure->name }, { "items", t.placed.size() }, { "problem", kProblems.at(static_cast<std::size_t>(t.problem)) }, { "affordable", t.affordable }, { "cost", t.cost }, { "turns", t.turns }, { "height", t.height }, { "ghost", !t.ghost_mesh.vertices.empty() } };
            if (t.at)
                placing["at"] = { { "col", t.at->col }, { "row", t.at->row } };
            return placing;
        });
        engine_.define("structures.marked", [this](const Json&) -> Json {
            const StructureTools& t = park_of(park_).structures;
            return std::ranges::count(t.marks, std::uint8_t { 1 });
        });
        engine_.define("rides.list", [this](const Json&) {
            const ParkScene& park = park_of(park_);
            Json rides = Json::array();
            for (std::size_t i = 0; i < park.rides.rides.size(); ++i)
            {
                const save::Ride& ride = park.rides.rides[i];
                const save::Station* station = ride.stations.empty() ? nullptr : &ride.stations.front();
                Json one { { "name", ride.base.name }, { "symbol", ride.base.symbol }, { "overlay", ride.base.overlay_path }, { "flat", ride.kind == save::RideKind::Flat }, { "site", spot_json(ride.base.spot) }, { "entrance", spot_json(station ? station->entrance_spot : std::nullopt) }, { "exit", spot_json(station ? station->exit_spot : std::nullopt) }, { "joined", false }, { "fence", ride.fence_symbol }, { "demolished", ride.base.demolished }, { "graphSamples", ride.graphs.speed.size() } };
                if (ride.track)
                {
                    one["pieces"] = ride.track->pieces.size();
                    one["trains"] = ride.trains.size();
                    one["carsPerTrain"] = ride.trains.empty() ? 0 : ride.trains.front().cars.size();
                    one["passengerCars"] = ride.cars_per_train;
                    Json symbols = Json::array();
                    for (const save::TrackPiece& p : ride.track->pieces)
                        symbols.push_back(p.symbol);
                    one["track"] = std::move(symbols);
                    Json platforms = Json::array();
                    for (const save::TrackPiece& p : ride.track->pieces)
                        for (const save::PlacedObject& o : p.platform ? park.objects : std::vector<save::PlacedObject> { })
                            if (o.owner_ref == p.platform->id && o.id != p.platform->id && !o.hidden)
                                platforms.push_back(o.symbol);
                    one["platforms"] = std::move(platforms);
                    Json arms = Json::array();
                    for (const save::TrackPiece& p : ride.track->pieces)
                        arms.push_back(p.arm_angles ? Json(std::vector<float>(p.arm_angles->begin(), p.arm_angles->end())) : Json(nullptr));
                    one["arms"] = std::move(arms);
                    // Where its cars stand now, as map points above the ground.
                    Json cars = Json::array();
                    for (std::size_t k = 0; k < park.car_spots.size() && k < park.car_places.size(); ++k)
                    {
                        if (park.car_spots[k].ride != i)
                            continue;
                        const glm::vec3 at = park.car_places[k][3];
                        const float col = (at.x - park.grid.origin_x) / park.grid.tile_size_x;
                        const float row = (at.z - park.grid.origin_z) / park.grid.tile_size_z;
                        cars.push_back({ { "col", col }, { "row", row }, { "up", at.y - park.grid.height_at(at.x, at.z).value_or(0) } });
                    }
                    one["cars"] = std::move(cars);
                }
                const entt::entity e = i < park.ride_entities.size() ? park.ride_entities[i] : entt::null;
                const auto& registry = park.sim.registry;
                if (e != entt::null && registry.valid(e))
                {
                    one["joined"] = true;
                    // How many of the simulation's attractions are this ride: one, unless it was taken on twice.
                    std::size_t simulated = 0;
                    for (const auto [_, a] : registry.view<const simulation::Attraction>().each())
                        simulated += a.saved == ride.base.id ? 1 : 0;
                    one["simulated"] = simulated;
                    if (const auto* fleet = registry.try_get<simulation::Trains>(e))
                    {
                        one["running"] = fleet->trains.size();
                        one["moving"] = std::ranges::count_if(fleet->trains, [](const simulation::Coaster& c) { return c.running; });
                    }
                    if (const auto* a = registry.try_get<simulation::Attraction>(e))
                    {
                        one["customers"] = a->customers;
                        one["price"] = a->price.value;
                        one["open"] = a->open;
                        one["onPaths"] = a->entrance != simulation::kNoNode;
                        // Where the guests walking to it are now.
                        Json heading = Json::array();
                        for (const auto [g, errand, pose] : registry.view<const simulation::Errand, const simulation::Pose>().each())
                            if (errand.kind == simulation::ErrandKind::Heading && errand.target == e)
                                heading.push_back({ { "col", (pose.position.x - park.grid.origin_x) / park.grid.tile_size_x }, { "row", (pose.position.z - park.grid.origin_z) / park.grid.tile_size_z } });
                        one["heading"] = std::move(heading);
                    }
                    if (const auto* c = registry.try_get<simulation::CycleRide>(e))
                    {
                        one["capacity"] = c->capacity;
                        one["cycleSeconds"] = simulation::cycle_seconds(*c);
                        one["queue"] = c->queue.size();
                        one["queueLine"] = c->line.size();
                        // A tracked ride's riders are aboard its trains.
                        std::size_t riders = c->riders.size();
                        if (const auto* fleet = registry.try_get<simulation::Trains>(e))
                            for (const auto& aboard : fleet->riders)
                                riders += aboard.size();
                        one["riders"] = riders;
                        one["ratings"] = { c->excitement.value, c->intensity.value, c->nausea.value };
                    }
                    if (const auto* wrecks = registry.try_get<simulation::Wrecks>(e))
                    {
                        Json flying = Json::array();
                        for (const auto& cars : wrecks->trains)
                            for (const simulation::Wreck& w : cars)
                                flying.push_back({ { "col", (w.at.x - park.grid.origin_x) / park.grid.tile_size_x }, { "row", (w.at.z - park.grid.origin_z) / park.grid.tile_size_z }, { "up", w.at.y - park.grid.height_at(w.at.x, w.at.z).value_or(0) }, { "state", w.state == simulation::WreckState::Flying ? "flying" : w.state == simulation::WreckState::Burning ? "burning" : "gone" } });
                        one["crashes"] = wrecks->crashes;
                        one["wrecks"] = std::move(flying);
                    }
                }
                rides.push_back(std::move(one));
            }
            return rides;
        });
        engine_.define("map.water", [this](const Json&) {
            grid_of(park_);
            Json lakes = Json::array();
            for (const save::WaterBody& b : park_->footpaths.water.bodies)
                lakes.push_back({ { "level", b.level }, { "triangles", b.tris.size() } });
            return lakes;
        });
        engine_.define("map.waterfalls", [this](const Json&) {
            const save::TerrainGrid& g = grid_of(park_);
            Json falls = Json::array();
            for (const save::Waterfall& f : park_->footpaths.waterfalls)
            {
                const glm::vec3 land = park::waterfall_point(f, 1.0f);
                const float x = (f.lip_left.x + f.lip_right.x) / 2;
                const float z = (f.lip_left.z + f.lip_right.z) / 2;
                falls.push_back({ { "col", (x - g.origin_x) / g.tile_size_x }, { "row", (z - g.origin_z) / g.tile_size_z }, { "landCol", (land.x - g.origin_x) / g.tile_size_x }, { "landRow", (land.z - g.origin_z) / g.tile_size_z }, { "top", f.top.y }, { "bottom", f.bottom.y }, { "width", std::hypot(f.lip_right.x - f.lip_left.x, f.lip_right.z - f.lip_left.z) } });
            }
            return falls;
        });
    }

    void ScriptHost::tick(const render::Device& device, ParkScene* park, FrontEndScene* front)
    {
        wheel_ = 0; // Turned for one frame only
        if (quit_)
            return;
        device_ = &device;
        park_ = park;
        front_ = front;
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
        return Pointer { at_->x * static_cast<float>(view.width), at_->y * static_cast<float>(view.height), down_, right_, middle_, shift_, control_, wheel_ };
    }

    void ScriptHost::write(const std::string& line, bool error)
    {
        if (error)
            logging::logger("script").error("{}", line);
        else
            logging::logger("script").info("{}", line);
        if (report_log_)
            report_log_ << (error ? "error: " : "") << line << std::endl;
    }

    void ScriptHost::fail(const std::string& why)
    {
        write(why, true);
        quit_ = 1;
    }

    std::optional<std::pair<ui::Widget&, float>> ScriptHost::shown_root() const
    {
        if (device_ == nullptr)
            return std::nullopt;
        if (park_ != nullptr)
            return std::pair<ui::Widget&, float> { park_->desktop->root(), park_interface_scale(*device_, *park_) };
        if (front_ != nullptr)
            return std::pair<ui::Widget&, float> { front_->live->desktop.root(), front_end_scale(*device_, *front_) };
        return std::nullopt;
    }

    std::optional<std::pair<int, std::size_t>> ScriptHost::take_career()
    {
        return std::exchange(career_, std::nullopt);
    }

    std::optional<std::string> ScriptHost::take_park()
    {
        return std::exchange(open_, std::nullopt);
    }

    std::optional<std::filesystem::path> ScriptHost::take_screenshot()
    {
        return std::exchange(screenshot_, std::nullopt);
    }

} // namespace rct3::scene
