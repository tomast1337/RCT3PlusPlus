// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/track_building.hpp"

#include "logging/log.hpp"
#include "park/asset_index.hpp"
#include "park/relief_maps.hpp"
#include "scene/park_scene.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/interface_assets.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/construction_panel.hpp"
#include "ui/screens/tool_row.hpp"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <utility>

namespace rct3::scene
{

    TrackModels::TrackModels() = default;
    TrackModels::~TrackModels() = default;
    TrackModels::TrackModels(TrackModels&&) noexcept = default;
    TrackModels& TrackModels::operator=(TrackModels&&) noexcept = default;

    namespace
    {
        constexpr std::string_view kPanelName = "construction";
        constexpr std::string_view kChainHelp = "ATE_TT_Chain";
        constexpr std::string_view kDemolishHelp = "ATE_TT_Demolish";
        constexpr std::string_view kCannotPlace = "IllegalTrackPlacement";
        constexpr std::string_view kCostLabel = "ShopItem_CostValue";
        constexpr std::string_view kPieceNames = "Track_"; ///< Followed by the section's name
        constexpr std::string_view kSectionTag = ":tks";
        /// Above the tool row, so the two never overlap.
        constexpr float kPanelBottom = ui::tool_row::kMargin + ui::tool_row::kButtonSize + 8;
        /// The ghost's tint and how much of what is behind it shows through.
        constexpr glm::vec4 kGhostTint { 0.75f, 0.9f, 1.0f, 0.55f };

        using park::kBankSteps;

        /// The panel's bank buttons run from hardest left to hardest right, upside down last; banks count left
        /// as positive.
        int panel_bank(std::size_t i)
        {
            return i + 1 == ui::construction_panel::kBanks ? park::kUpsideDown : kBankSteps - 1 - static_cast<int>(i);
        }

        std::string slashes(std::string path)
        {
            std::ranges::replace(path, '\\', '/');
            return path;
        }

        const park::PieceCatalogue* catalogue(const TrackBuilding& b)
        {
            const auto found = b.catalogues.find(b.overlay);
            return found != b.catalogues.end() ? &found->second : nullptr;
        }

        /// Of the scene's objects by id.
        std::unordered_map<std::uint64_t, std::size_t> object_indices(const ParkScene& scene)
        {
            std::unordered_map<std::uint64_t, std::size_t> out;
            for (std::size_t i = 0; i < scene.objects.size(); ++i)
                out.emplace(scene.objects[i].id, i);
            return out;
        }

        /// The open end of the ride's track: past its last piece, unless the track closes on itself.
        std::optional<park::TrackTip> track_tip(const ParkScene& scene, const TrackBuilding& b)
        {
            const park::PieceCatalogue* c = catalogue(b);
            if (c == nullptr || !b.ride)
                return std::nullopt;
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (!ride.track || ride.track->pieces.empty() || ride.track->circuit)
                return std::nullopt;
            const save::TrackPiece& last = ride.track->pieces.back();
            const auto objects = object_indices(scene);
            const auto object = objects.find(last.scenery);
            const park::PieceShape* shape = park::shape_named(*c, last.symbol);
            if (object == objects.end() || shape == nullptr)
                return std::nullopt;
            return park::tip_of(scene.objects[object->second], *shape, last.reversed, scene.grid);
        }

        /// Recomputes what fits the open end, the piece picked and its ghost.
        void refresh(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            const park::PieceCatalogue* c = catalogue(b);
            b.candidates.clear();
            b.picked.reset();
            b.ghost.reset();
            b.ghost_mesh = { };
            if (c == nullptr || !b.tip)
                return;
            b.candidates = park::pieces_after(c->pieces, b.tip->end);
            b.choice = park::settle_choice(c->pieces, b.candidates, b.choice);
            b.picked = park::chosen_piece(c->pieces, b.candidates, b.choice);
            if (!b.picked)
                return;
            const park::PieceShape& shape = c->pieces[b.picked->index];
            b.ghost = park::place_after(*b.tip, shape, b.picked->reversed, scene.grid);
            if (!b.ghost)
                return;
            save::PlacedObject& o = b.ghost->object;
            o.overlay = b.overlay;
            if (!load_object_model(scene, b.models, assets, o))
                return;
            const std::array<save::PlacedObject, 1> one { o };
            b.ghost_mesh = park::object_mesh(one, scene.models, scene.grid);
            for (park::GroundVertex& v : b.ghost_mesh.vertices)
                v.colour *= kGhostTint;
        }

        ui::IconState state_of(bool picked, bool offered)
        {
            return picked ? ui::IconState::On : offered ? ui::IconState::Off : ui::IconState::Dis;
        }

        std::u16string piece_name(const ui::StringTable& strings, std::string_view name)
        {
            const std::u16string_view text = strings.get_ignoring_case(std::string(kPieceNames) + std::string(name));
            return text.empty() ? strings::to_utf16(name) : std::u16string(text);
        }

        /// The panel's view of what the building offers now.
        ui::ConstructionView panel_view(const ParkScene& scene, const TrackBuilding& b, const ui::InterfaceAssets& assets, int currency)
        {
            ui::ConstructionView v;
            v.curves.fill(ui::IconState::Dis);
            v.slopes.fill(ui::IconState::Dis);
            v.banks.fill(ui::IconState::Dis);
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            v.can_demolish = ride.track && !ride.track->pieces.empty();
            const park::PieceCatalogue* c = catalogue(b);
            if (c == nullptr || !b.tip)
                return v;
            const park::ChoiceOptions o = park::choice_options(c->pieces, b.candidates, b.choice);
            const bool by_shape = !b.choice.named.has_value();
            for (int curve = -park::kCurveSizes; curve <= park::kCurveSizes; ++curve)
                v.curves.at(park::curve_index(curve)) = state_of(by_shape && curve == b.choice.curve, o.curves.at(park::curve_index(curve)));
            for (int slope = -park::kSlopeSteps; slope <= park::kSlopeSteps; ++slope)
                v.slopes.at(park::slope_index(slope)) = state_of(by_shape && slope == b.choice.slope, o.slopes.at(park::slope_index(slope)));
            for (std::size_t i = 0; i < ui::construction_panel::kBanks; ++i)
            {
                const int bank = panel_bank(i);
                v.banks.at(i) = state_of(by_shape && bank == b.choice.bank, o.banks.at(park::bank_index(bank)));
            }
            if (o.any_chain)
                v.chain = state_of(b.choice.chain && o.chain, o.chain);
            for (std::size_t i = 0; i < o.named.size(); ++i)
            {
                v.named.push_back(piece_name(*assets.strings, c->pieces[o.named[i].index].record.name));
                if (b.choice.named == o.named[i])
                    v.named_on = i;
            }
            if (b.picked)
            {
                const park::PieceShape& shape = c->pieces[b.picked->index];
                v.piece = piece_name(*assets.strings, shape.record.name);
                if (!b.ghost)
                    v.piece = assets.strings->get(kCannotPlace);
                // The price the piece's scenery item stores, in hundredths as the park's money is.
                v.cost = std::u16string(assets.strings->get(kCostLabel)) + u" " + ui::money_text(ui::currency(*assets.strings, currency), shape.record.cost);
            }
            return v;
        }

        /// Adds the ghost to the park and to the ride's track.
        void build(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            const park::PieceCatalogue* c = catalogue(b);
            save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (c == nullptr || !b.ghost || !b.picked || !ride.track)
                return;
            wait_for_build(scene);
            std::uint64_t last_id = 0;
            for (const save::PlacedObject& o : scene.objects)
                last_id = std::max(last_id, o.id);
            save::PlacedObject object = b.ghost->object;
            object.id = last_id + 1;
            object.owner_ref = object.id;
            const auto objects = object_indices(scene);
            save::TrackPiece piece;
            if (!ride.track->pieces.empty())
            {
                const save::TrackPiece& before = ride.track->pieces.back();
                piece.colours = before.colours;
                piece.segment = before.segment;
                piece.asset = before.asset;
                if (const auto found = objects.find(before.scenery); found != objects.end())
                    object.colours = scene.objects[found->second].colours;
            }
            const park::PieceShape& shape = c->pieces[b.picked->index];
            piece.id = object.id;
            piece.symbol = shape.record.name + std::string(kSectionTag);
            piece.scenery = object.id;
            piece.tile_x = object.col;
            piece.tile_z = object.row;
            piece.height = object.base_height.value_or(0);
            piece.direction = static_cast<std::uint8_t>(object.rotation);
            piece.reversed = b.picked->reversed;
            ride.track->pieces.push_back(piece);
            if (!ride.track->segments.empty())
                ++ride.track->segments.back().piece_count;
            scene.attractions[object.id] = { park::AttractionRef::Kind::Ride, *b.ride };
            scene.objects.push_back(std::move(object));
            objects_changed(scene);
            b.tip = b.ghost->next;
            refresh(scene, b, assets);
        }

        /// Takes the last piece out of the park and off the ride's track, which opens a closed circuit.
        void demolish(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (!ride.track || ride.track->pieces.empty())
                return;
            wait_for_build(scene);
            const save::TrackPiece last = ride.track->pieces.back();
            ride.track->pieces.pop_back();
            ride.track->circuit = false;
            if (!ride.track->segments.empty() && ride.track->segments.back().piece_count > 0)
                --ride.track->segments.back().piece_count;
            const auto objects = object_indices(scene);
            if (const auto found = objects.find(last.scenery); found != objects.end())
                scene.objects[found->second].hidden = true;
            scene.attractions.erase(last.scenery);
            objects_changed(scene);
            b.tip = track_tip(scene, b);
            refresh(scene, b, assets);
        }

    } // namespace

    bool load_object_model(ParkScene& scene, std::map<std::string, TrackModels, std::less<>>& cache, const ui::InterfaceAssets& assets, const save::PlacedObject& object)
    {
        const std::string key = park::model_key(object);
        if (scene.models.contains(key))
            return true;
        if (!assets.install)
            return false;
        auto [found, added] = cache.try_emplace(object.overlay);
        TrackModels& m = found->second;
        if (added)
        {
            // Only the overlay and those it takes assets from: scanning the whole track folder would be slow.
            m.index = std::make_unique<park::AssetIndex>(park::AssetIndex::scan(assets.install->root(), { }));
            if (const auto file = assets.install->find(slashes(object.overlay) + ".common.ovl"))
                m.index->add_file(*file);
            std::set<std::string, std::less<>> tried;
            for (auto missing = m.index->missing_references(); !missing.empty(); missing = m.index->missing_references())
            {
                bool more = false;
                for (const std::string& reference : missing)
                {
                    if (!tried.insert(reference).second)
                        continue;
                    if (const auto file = assets.install->find(reference + ".common.ovl"))
                        more = m.index->add_file(*file) || more;
                }
                if (!more)
                    break;
            }
            m.loader = std::make_unique<park::ModelLoader>(*m.index);
        }
        auto model = m.loader->load(object.symbol, object.overlay);
        if (!model)
            return false;
        scene.models.emplace(key, std::move(*model));
        for (const auto& [name, picture] : m.loader->pictures())
            scene.pictures.try_emplace(name, picture);
        return true;
    }

    void objects_changed(ParkScene& scene)
    {
        scene.anchors = park::object_anchors(scene.objects, scene.models, scene.grid, scene.fitted);
        scene.levels = { };
        scene.since_rebuild = kObjectRebuildInterval;
    }

    void wait_for_build(ParkScene& scene)
    {
        if (scene.object_build.valid())
            (void)scene.object_build.get();
    }

    void ghost_textures(render::Device& device, ParkScene& scene, const park::ObjectMesh& ghost)
    {
        std::vector<park::ObjectBatch> missing;
        for (const park::ObjectBatch& batch : ghost.batches)
        {
            if (!scene.object_textures.contains(batch.texture))
                missing.push_back(batch);
        }
        if (missing.empty())
            return;
        auto pictures = park::paint_batches(missing, scene.pictures);
        if (scene.relief_maps)
            park::add_relief_maps(pictures); // The pictures stay for the object once it is built
        auto uploaded = ui::upload_textures(device, pictures);
        if (uploaded)
            scene.object_textures.merge(*uploaded);
    }

    void close_track_building(ParkScene& scene)
    {
        scene.building.ride.reset();
        scene.building.ghost.reset();
        scene.building.ghost_mesh = { };
        if (ui::Widget* panel = scene.desktop->root().find(kPanelName))
            panel->close();
    }

    std::optional<std::string> ride_track_overlay(const ParkScene& scene, std::size_t ride)
    {
        if (ride >= scene.rides.rides.size() || !scene.rides.rides[ride].track)
            return std::nullopt;
        const auto objects = object_indices(scene);
        for (const save::TrackPiece& piece : scene.rides.rides[ride].track->pieces)
        {
            if (const auto found = objects.find(piece.scenery); found != objects.end() && !scene.objects[found->second].overlay.empty())
                return scene.objects[found->second].overlay;
        }
        return std::nullopt;
    }

    bool open_track_building(ParkScene& scene, std::size_t ride, std::string overlay, const ui::InterfaceAssets& assets)
    {
        if (ride >= scene.rides.rides.size() || !scene.rides.rides[ride].track || !assets.install)
            return false;
        TrackBuilding& b = scene.building;
        if (!b.catalogues.contains(overlay))
        {
            const auto file = assets.install->find(slashes(overlay) + ".common.ovl");
            if (!file)
                return false;
            auto loaded = park::load_piece_catalogue(*file, overlay);
            if (!loaded)
            {
                logging::logger("park").warning("track pieces of {}: {}", overlay, loaded.error());
                return false;
            }
            b.catalogues.emplace(overlay, std::move(*loaded));
        }
        b.ride = ride;
        b.overlay = std::move(overlay);
        b.choice = { };
        b.tip = track_tip(scene, b);
        refresh(scene, b, assets);
        if (scene.desktop->root().find(kPanelName) == nullptr)
        {
            auto& panel = scene.desktop->root().add<ui::ConstructionPanel>(std::string(kPanelName), ui::game_skins::construction_panel_style(), std::u16string(assets.strings->get(kChainHelp)), std::u16string(assets.strings->get(kDemolishHelp)));
            panel.placement = ui::bottom_left(0, kPanelBottom, ui::construction_panel::kWidth, ui::construction_panel::kHeight);
        }
        return true;
    }

    void update_track_building(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, int currency)
    {
        TrackBuilding& b = scene.building;
        auto* panel = static_cast<ui::ConstructionPanel*>(scene.desktop->root().find(kPanelName));
        if (panel == nullptr || !b.ride)
            return;
        if (panel->take_closed())
        {
            close_track_building(scene);
            return;
        }
        const park::PieceCatalogue* c = catalogue(b);
        bool changed = false;
        while (const auto event = panel->take_event())
        {
            if (c == nullptr)
                break;
            using enum ui::ConstructionAction;
            switch (event->action)
            {
                case Curve:
                    b.choice = park::pick_value(c->pieces, b.candidates, b.choice, park::ChoiceAxis::Curve, static_cast<int>(event->index) - park::kCurveSizes);
                    break;
                case Slope:
                    b.choice = park::pick_value(c->pieces, b.candidates, b.choice, park::ChoiceAxis::Slope, static_cast<int>(event->index) - park::kSlopeSteps);
                    break;
                case Bank:
                    b.choice = park::pick_value(c->pieces, b.candidates, b.choice, park::ChoiceAxis::Bank, panel_bank(event->index));
                    break;
                case Chain:
                    b.choice = park::pick_chain(c->pieces, b.candidates, b.choice);
                    break;
                case Named:
                {
                    const park::ChoiceOptions o = park::choice_options(c->pieces, b.candidates, b.choice);
                    if (event->index < o.named.size())
                        b.choice.named = o.named[event->index];
                    break;
                }
                case Build:
                    build(scene, b, assets);
                    break;
                case Demolish:
                    demolish(scene, b, assets);
                    break;
            }
            changed = true;
        }
        if (changed)
            refresh(scene, b, assets);
        ghost_textures(device, scene, b.ghost_mesh);
        panel->show(panel_view(scene, b, assets, currency));
    }

    void draw_track_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.building.ride)
            draw_ghost_mesh(device, scene, scene.building.ghost_mesh, transform, light);
    }

    void draw_ghost_mesh(render::Device& device, const ParkScene& scene, const park::ObjectMesh& mesh, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (mesh.indices.empty() || !scene.see_through_pipeline)
            return;
        const auto vertices = device.allocate_transient(std::as_bytes(std::span(mesh.vertices)));
        const auto indices = device.allocate_transient(std::as_bytes(std::span(mesh.indices)));
        if (!vertices || !indices)
            return;
        for (const park::ObjectBatch& batch : mesh.batches)
        {
            const auto texture = scene.object_textures.find(batch.texture);
            render::DrawCommand draw;
            draw.pipeline = scene.see_through_pipeline;
            draw.vertices = *vertices;
            draw.indices = *indices;
            draw.index_type = render::IndexType::U32;
            draw.first = batch.first;
            draw.count = batch.count;
            draw.transform = transform;
            draw.texture = texture != scene.object_textures.end() ? texture->second.handle : scene.blank;
            draw.filter = render::Filter::Linear;
            draw.address = render::AddressMode::Repeat;
            draw.light = light;
            device.draw(draw);
        }
    }

} // namespace rct3::scene
