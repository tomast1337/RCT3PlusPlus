// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/script_host.hpp"

#include "assets/embedded_files.hpp"
#include "logging/log.hpp"
#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "scene/park_scene.hpp"
#include "simulation/components.hpp"
#include "simulation/systems.hpp"
#include "ui/widgets/button.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <format>
#include <future>
#include <span>
#include <sstream>

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

    std::unique_ptr<ScriptHost> ScriptHost::start(const std::filesystem::path& file, std::optional<std::filesystem::path> report)
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
        engine_.define("pointer.press", [this](const Json&) {
            down_ = true;
            return Json(nullptr);
        });
        engine_.define("pointer.release", [this](const Json&) {
            down_ = false;
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
        engine_.define("ui.find", [this](const Json& a) -> Json {
            if (a.empty() || !a[0].is_string())
                throw std::invalid_argument("ui.find takes a widget path");
            if (park_ == nullptr || device_ == nullptr)
                return nullptr;
            auto w = park_->desktop->root().find(a[0].get<std::string>());
            const render::Extent view = device_->extent();
            if (!w || !w->visible || view.width == 0 || view.height == 0)
                return nullptr;
            const float scale = park_interface_scale(*device_, *park_);
            const ui::Rect& r = w->rect();
            Json found { { "x", (r.x0 + r.x1) / 2 * scale / static_cast<float>(view.width) }, { "y", (r.y0 + r.y1) / 2 * scale / static_cast<float>(view.height) } };
            if (const auto* b = dynamic_cast<const ui::Button*>(as_pointer(w)); b != nullptr && b->toggle)
                found["on"] = b->on;
            return found;
        });
        engine_.define("ui.children", [this](const Json& a) -> Json {
            if (a.empty() || !a[0].is_string())
                throw std::invalid_argument("ui.children takes a widget path");
            if (park_ == nullptr)
                return nullptr;
            auto w = park_->desktop->root().find(a[0].get<std::string>());
            if (!w || !w->visible)
                return nullptr;
            Json names = Json::array();
            for (const auto& child : w->children())
                if (child->visible)
                    names.push_back(child->name());
            return names;
        });
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
            return { { "corners", c.corners }, { "surface", c.surface }, { "cliff", c.cliff }, { "owned", park::owned_outright(c) } };
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
        engine_.define("build.flatRides", [this](const Json&) -> Json {
            const auto items = build_items(park_of(park_));
            if (!items)
                return nullptr;
            Json rides = Json::array();
            for (const park::BuildItem& item : *items)
                if (item.kind == park::BuildKind::FlatRide)
                    rides.push_back({ { "symbol", item.symbol }, { "overlay", item.overlay }, { "name", item.name_text }, { "width", item.width }, { "length", item.length }, { "price", item.price }, { "doors", !item.entrance.empty() && !item.exit.empty() } });
            return rides;
        });
        engine_.define("build.pick", [this](const Json& a) -> Json {
            if (a.size() < 2 || !a[0].is_string() || !a[1].is_string())
                throw std::invalid_argument("build.pick takes an overlay and a symbol");
            ParkScene& park = park_of(park_);
            const auto items = build_items(park);
            if (!items)
                return false;
            const auto found = std::ranges::find_if(*items, [&](const park::BuildItem& item) { return item.overlay == a[0].get<std::string>() && item.symbol == a[1].get<std::string>(); });
            if (found == items->end())
                return false;
            // A ride built piece by piece needs the interface's assets, which it gets as a pick from a list does.
            if (found->kind == park::BuildKind::TrackedRide)
                park.build.asked = *found;
            return found->kind == park::BuildKind::TrackedRide || start_ride_placing(park, *found);
        });
        engine_.define("build.construction", [this](const Json&) -> Json {
            const TrackBuilding& b = park_of(park_).building;
            if (!b.ride)
                return nullptr;
            const auto c = b.catalogues.find(b.overlay);
            const bool named = b.picked && c != b.catalogues.end();
            return { { "ride", *b.ride }, { "offered", b.candidates.size() }, { "piece", named ? Json(c->second.pieces[b.picked->index].record.name) : Json(nullptr) }, { "ghost", b.ghost.has_value() } };
        });
        engine_.define("build.stop", [this](const Json&) {
            stop_ride_placing(park_of(park_));
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
        engine_.define("rides.show", [this](const Json& a) {
            ParkScene& park = park_of(park_);
            if (!a.at(0).is_number_unsigned() || a[0].get<std::size_t>() >= park.rides.rides.size())
                throw std::invalid_argument("rides.show takes a ride's place in rides.list()");
            park.info_requested = park::AttractionRef { park::AttractionRef::Kind::Ride, a[0].get<std::size_t>() };
            return Json(nullptr);
        });
        engine_.define("build.placing", [this](const Json&) -> Json {
            const RidePlacing& p = park_of(park_).placing;
            if (!p.item)
                return nullptr;
            constexpr std::array<std::string_view, 4> kSteps { "ride", "entrance", "exit", "scenery" };
            constexpr std::array<std::string_view, 4> kProblems { "none", "not owned", "path", "blocked" };
            return { { "step", kSteps.at(static_cast<std::size_t>(p.step)) }, { "problem", kProblems.at(static_cast<std::size_t>(p.problem)) }, { "affordable", p.affordable } };
        });
        engine_.define("rides.list", [this](const Json&) {
            const ParkScene& park = park_of(park_);
            Json rides = Json::array();
            for (std::size_t i = 0; i < park.rides.rides.size(); ++i)
            {
                const save::Ride& ride = park.rides.rides[i];
                const save::Station* station = ride.stations.empty() ? nullptr : &ride.stations.front();
                Json one { { "name", ride.base.name }, { "symbol", ride.base.symbol }, { "overlay", ride.base.overlay_path }, { "flat", ride.kind == save::RideKind::Flat }, { "site", spot_json(ride.base.spot) }, { "entrance", spot_json(station ? station->entrance_spot : std::nullopt) }, { "exit", spot_json(station ? station->exit_spot : std::nullopt) }, { "joined", false } };
                if (ride.track)
                {
                    one["pieces"] = ride.track->pieces.size();
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
                    if (const auto* a = registry.try_get<simulation::Attraction>(e))
                    {
                        one["customers"] = a->customers;
                        one["price"] = a->price;
                        one["open"] = a->open;
                        one["onPaths"] = a->entrance != simulation::kNoNode;
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
                        one["ratings"] = { c->excitement, c->intensity, c->nausea };
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
        return Pointer { at_->x * static_cast<float>(view.width), at_->y * static_cast<float>(view.height), down_, shift_, control_ };
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

    std::optional<std::string> ScriptHost::take_park()
    {
        return std::exchange(open_, std::nullopt);
    }

    std::optional<std::filesystem::path> ScriptHost::take_screenshot()
    {
        return std::exchange(screenshot_, std::nullopt);
    }

} // namespace rct3::scene
