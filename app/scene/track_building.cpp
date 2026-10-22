// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/track_building.hpp"

#include "config/game_saves.hpp"
#include "core/maybe.hpp"
#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "ovl/ride_train.hpp"
#include "ovl/tracked_ride.hpp"
#include "park/asset_index.hpp"
#include "park/object_models.hpp"
#include "park/relief_maps.hpp"
#include "park/ride_placement.hpp"
#include "park/track_completion.hpp"
#include "park/track_rules.hpp"
#include "park/track_supports.hpp"
#include "park/water_fill.hpp"
#include "save/save_file.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "scene/ride_placing.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/interface_assets.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/construction_panel.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/screens/tool_row.hpp"
#include "ui/widgets/dropdown.hpp"
#include "ui/widgets/panel.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/tickbox.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <tuple>
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
        constexpr std::string_view kArmName = "construction_arm";       ///< The strip of arm angles above the panel
        constexpr std::string_view kDesignName = "construction_design"; ///< The saved designs, above the panel
        constexpr std::string_view kDesignFolder = "Coaster Designs";
        constexpr std::string_view kDesignExtension = ".trk";
        constexpr std::string_view kCustomTrack = "TrackBrowser_CustomTrack";
        constexpr std::array<std::string_view, 4> kArmJoints { "base", "lower", "upper", "head" };
        constexpr std::string_view kArmEnable = "ATE_RoboCoaster_Enable";
        constexpr std::string_view kArmEnableHelp = "ATE_TT_RoboCoaster_Enable";
        constexpr std::string_view kArmZeroHelp = "ATE_TT_RoboCoaster_Zero";
        constexpr std::string_view kArmPrevHelp = "ATE_TT_RoboCoaster_CopyFromPrev";
        constexpr std::string_view kArmNextHelp = "ATE_TT_RoboCoaster_CopyFromNext";
        constexpr std::string_view kArmAngleHelp = "ATE_TT_SpinAngle";
        constexpr std::string_view kPieceBackHelp = "ATE_TT_Back";
        constexpr std::string_view kPieceForwardHelp = "ATE_TT_Forward";
        /// How far each joint may be turned either way, degrees, and the spinners' step.
        constexpr int kArmTurns = 360;
        constexpr int kArmStep = 10;
        constexpr std::string_view kChainHelp = "ATE_TT_Chain";
        constexpr std::string_view kDemolishHelp = "ATE_TT_Demolish";
        constexpr std::string_view kCannotPlace = "IllegalTrackPlacement";
        constexpr std::string_view kCostLabel = "ShopItem_CostValue";
        constexpr std::string_view kCompleteHelp = "ATE_TT_Autocomplete";
        constexpr std::string_view kNewSegmentHelp = "ATE_TT_NewTrackSegment";
        /// Ends tried before finishing a track gives up, a few seconds at most.
        constexpr std::size_t kCompletionTries = 20000;
        /// How near a piece's end must come to where the first piece starts to close the track, in park units.
        constexpr float kCloses = 0.1f;
        constexpr std::string_view kPieceNames = "Track_"; ///< Followed by the section's name
        constexpr std::string_view kSectionTag = ":tks";
        /// Above the tool row, so the two never overlap.
        constexpr float kPanelBottom = ui::tool_row::kMargin + ui::tool_row::kButtonSize + 8;
        /// The ghost's tint and how much of what is behind it shows through.

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

        /// The pieces of the track overlay `overlay`, read once.
        std::optional<const park::PieceCatalogue&> load_catalogue(TrackBuilding& b, const std::string& overlay, const ui::InterfaceAssets& assets)
        {
            if (const auto found = b.catalogues.find(overlay); found != b.catalogues.end())
                return found->second;
            const auto file = assets.install ? assets.install->find(std::format("{}.common.ovl", slashes(overlay))) : std::nullopt;
            if (!file)
                return std::nullopt;
            auto loaded = park::load_piece_catalogue(*file, overlay);
            if (!loaded)
            {
                logging::logger("park").warning("track pieces of {}: {}", overlay, loaded.error());
                return std::nullopt;
            }
            return b.catalogues.emplace(overlay, std::move(*loaded)).first->second;
        }

        /// Of the scene's objects by id.
        std::unordered_map<std::uint64_t, std::size_t> object_indices(const ParkScene& scene)
        {
            std::unordered_map<std::uint64_t, std::size_t> out;
            for (std::size_t i = 0; i < scene.objects.size(); ++i)
                out.emplace(scene.objects[i].id, i);
            return out;
        }

        /// Whether the ride's track waits for the first piece of a stretch: it has none yet, or a new one was begun.
        bool opening(const save::Ride& ride)
        {
            return ride.track && (ride.track->pieces.empty() || (!ride.track->segments.empty() && ride.track->segments.back().piece_count == 0));
        }

        /// The first piece of the stretch being built, where laying round to it closes it.
        const save::TrackPiece& stretch_start(const save::Track& track)
        {
            return track.pieces.at(track.segments.empty() ? 0 : track.segments.back().first_piece);
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
            auto shape = park::shape_named(*c, last.symbol);
            if (object == objects.end() || !shape)
                return std::nullopt;
            return park::tip_of(scene.objects[object->second], *shape, last.reversed, scene.grid);
        }

        /// The catalogue's piece the ride's track ends with, laid as it is.
        std::optional<park::LaidPiece> last_piece(const ParkScene& scene, const TrackBuilding& b)
        {
            const park::PieceCatalogue* c = catalogue(b);
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (c == nullptr || !ride.track || ride.track->pieces.empty())
                return std::nullopt;
            const save::TrackPiece& last = ride.track->pieces.back();
            const auto shape = park::shape_named(*c, last.symbol);
            if (!shape)
                return std::nullopt;
            return park::LaidPiece { static_cast<std::size_t>(&*shape - c->pieces.data()), last.reversed };
        }

        /// A saved design: its ride, and the scenery placed with it, which is the ride's doors.
        struct SavedDesign
        {
            save::Ride ride;
            std::vector<save::PlacedObject> scenery;
        };

        /// The design saved in `file`; nothing when the file cannot be read or holds no track.
        std::optional<SavedDesign> design_ride(const std::filesystem::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto saved = save::read_save(std::as_bytes(std::span(raw)), { });
            if (!saved)
                return std::nullopt;
            const save::ObjectIndex index(*saved);
            auto rides = save::load_rides(*saved, index);
            if (!rides || rides->rides.empty() || !rides->rides.front().track)
                return std::nullopt;
            auto world = save::load_world(*saved, index);
            return SavedDesign { std::move(rides->rides.front()), world ? std::move(world->objects) : std::vector<save::PlacedObject> { } };
        }

        /// Whether `o`, a piece of the ride being built, goes only on water and has dry land under it. Pieces of
        /// other rides may run under water, as rides that splash through it need.
        bool off_water(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, save::PlacedObject o)
        {
            o.overlay = b.overlay;
            if (!load_object_model(scene, b.models, assets, o))
                return false;
            const auto model = scene.models.find(park::model_key(o));
            if (model == scene.models.end() || !model->second.water_only)
                return false;
            const park::SupportFootprint shape { .x_squares = model->second.squares[0], .z_squares = model->second.squares[1] };
            return !std::ranges::all_of(park::footprint_tiles(o, shape), [&](const auto& t) { return park::holds_water(scene.footpaths.water.bodies, { t.first, t.second }); });
        }

        /// Recomputes what fits the open end, the piece picked and its ghost.
        void refresh(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            const park::PieceCatalogue* c = catalogue(b);
            b.candidates.clear();
            b.picked.reset();
            b.ghost.reset();
            b.ghost_mesh = { };
            b.off_water = false;
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            const bool starting = opening(ride);
            b.backward = false;
            const park::TrackRules rules = *b.ride < scene.track_rules.size() ? scene.track_rules[*b.ride] : park::TrackRules { };
            if (!starting)
                b.tip = track_tip(scene, b);
            if (c == nullptr || (starting ? !b.aim : !b.tip) || (!starting && rules.one_piece))
                return;
            b.candidates = starting ? park::starting_pieces(c->pieces) : park::pieces_after(c->pieces, b.tip->end, last_piece(scene, b));
            // A ride built back from its station goes on from where its first piece starts: pieces laid backwards
            // from there, then kept in the track's own direction.
            // shortcut: the way follows the ride's file, never the player, upgrade with a forward and backward button.
            if (!starting && rules.builds_back)
            {
                b.candidates.clear();
                b.backward = true;
                const save::TrackPiece& first = ride.track->pieces.front();
                const auto objects = object_indices(scene);
                const auto object = objects.find(first.scenery);
                const auto shape = park::shape_named(*c, first.symbol);
                if (object != objects.end() && shape)
                {
                    // The first piece laid backwards, and its far end.
                    const park::LaidPiece turned { static_cast<std::size_t>(&*shape - c->pieces.data()), !first.reversed };
                    b.tip = park::tip_of(scene.objects[object->second], *shape, turned.reversed, scene.grid);
                    b.candidates = park::pieces_after(c->pieces, b.tip->end, turned);
                }
            }
            // Only pieces that can be laid there, so a piece offered one way round is not hidden by the other.
            if (!starting)
                std::erase_if(b.candidates, [&](const park::LaidPiece& l) { return !park::place_after(*b.tip, c->pieces[l.index], l.reversed, scene.grid); });
            b.choice = park::settle_choice(c->pieces, b.candidates, b.choice);
            b.picked = park::chosen_piece(c->pieces, b.candidates, b.choice);
            if (starting && !b.design.empty())
            {
                // A design starts with its own first piece, wherever the choice stands.
                const auto first = park::shape_named(*c, b.design.front().symbol);
                b.picked = first ? std::optional(park::LaidPiece { static_cast<std::size_t>(&*first - c->pieces.data()), b.design.front().reversed }) : std::nullopt;
            }
            if (!b.picked)
                return;
            const park::PieceShape& shape = c->pieces[b.picked->index];
            if (starting)
            {
                // Laid from where it would start standing on the aimed tile.
                save::PlacedObject on;
                on.col = b.aim->col;
                on.row = b.aim->row;
                on.rotation = b.turns;
                on.symbol = shape.record.item;
                on.overlay = b.overlay;
                // A piece that goes only on water floats on it, not on the lake's floor.
                const auto surface = park::water_surface(scene.footpaths.water.bodies, *b.aim);
                if (const auto model = load_object_model(scene, b.models, assets, on) ? scene.models.find(park::model_key(on)) : scene.models.end(); surface && model != scene.models.end() && model->second.water_only)
                {
                    on.base_height = *surface;
                    on.force_absolute_height = true;
                }
                b.tip = park::start_of(on, shape, b.picked->reversed, scene.grid);
            }
            b.ghost = park::place_after(*b.tip, shape, b.picked->reversed, scene.grid);
            if (!b.ghost)
                return;
            save::PlacedObject& o = b.ghost->object;
            o.overlay = b.overlay;
            if (!load_object_model(scene, b.models, assets, o))
                return;
            b.off_water = off_water(scene, b, assets, o);
            // A design shows whole, as far as its pieces go on from the first.
            std::vector<save::PlacedObject> shown { o };
            park::TrackTip tip = b.ghost->next;
            for (std::size_t i = 1; starting && i < b.design.size(); ++i)
            {
                const auto next = park::shape_named(*c, b.design[i].symbol);
                auto placed = next ? park::place_after(tip, *next, b.design[i].reversed, scene.grid) : std::nullopt;
                if (!placed)
                    break;
                placed->object.overlay = b.overlay;
                if (!load_object_model(scene, b.models, assets, placed->object))
                    break;
                tip = placed->next;
                shown.push_back(std::move(placed->object));
            }
            b.ghost_mesh = park::object_mesh(shown, scene.models, scene.grid);
            for (park::GroundVertex& v : b.ghost_mesh.vertices)
                v.colour *= b.off_water ? kRefusedTint : kGhostTint;
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
            v.can_complete = v.can_demolish && b.tip.has_value() && !b.backward && !opening(ride);
            // A ride whose track may lie in several stretches, or a water transport, can begin another.
            if (const park::TrackRules rules = *b.ride < scene.track_rules.size() ? scene.track_rules[*b.ride] : park::TrackRules { }; rules.may_be_split || ride.mode == save::OperatingMode::WaterTransport)
                v.new_segment = v.can_demolish && !opening(ride) && !b.backward;
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
                else if (b.off_water)
                    v.piece = assets.strings->get("PlaceObject_MustBeOnWater");
                if (!b.complaint.empty())
                {
                    // Some say which ride they are about.
                    v.piece = assets.strings->get(b.complaint);
                    constexpr std::u16string_view kNameMark = u"%NAME%";
                    if (const auto at = v.piece.find(kNameMark); at != std::u16string::npos)
                        v.piece.replace(at, kNameMark.size(), ui::attraction_name(scene.rides.rides.at(*b.ride).base, *assets.strings));
                }
                // The price the piece's scenery item stores, in hundredths as the park's money is.
                v.cost = std::u16string(assets.strings->get(kCostLabel)) + u" " + ui::money_text(ui::currency(*assets.strings, currency), shape.record.cost);
            }
            return v;
        }

        /// After a piece is built or taken away: swaps the pieces the ride's patterns match for the pieces they
        /// become, each laid where the one it replaces started, and measures the track's course again.
        void track_changed(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            const park::PieceCatalogue* c = catalogue(b);
            save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (c == nullptr || !ride.track)
                return;
            std::vector<park::LaidPiece> laid;
            for (const save::TrackPiece& p : ride.track->pieces)
                if (const auto shape = park::shape_named(*c, p.symbol))
                    laid.push_back({ static_cast<std::size_t>(&*shape - c->pieces.data()), p.reversed });
            const bool patterned = laid.size() == ride.track->pieces.size() && *b.ride < scene.track_rules.size();
            const std::vector<park::LaidPiece> swapped = patterned ? park::apply_patterns(c->pieces, laid, scene.track_rules[*b.ride].patterns, ride.track->circuit) : laid;
            if (swapped != laid)
            {
                wait_for_build(scene);
                const auto objects = object_indices(scene);
                const auto object_of = [&](const save::TrackPiece& p) {
                    const auto found = objects.find(p.scenery);
                    return found != objects.end() ? maybe(&scene.objects[found->second]) : std::nullopt;
                };
                for (std::size_t i = 0; i < laid.size(); ++i)
                {
                    save::TrackPiece& piece = ride.track->pieces[i];
                    const auto object = object_of(piece);
                    if (swapped[i] == laid[i] || !object)
                        continue;
                    // Where the piece starts: past the piece before it, or where it starts itself.
                    const auto before = i > 0 ? object_of(ride.track->pieces[i - 1]) : std::nullopt;
                    const park::TrackTip start = before ? park::tip_of(*before, c->pieces[swapped[i - 1].index], swapped[i - 1].reversed, scene.grid) : park::start_of(*object, c->pieces[laid[i].index], laid[i].reversed, scene.grid);
                    const park::PieceShape& shape = c->pieces[swapped[i].index];
                    const auto placed = park::place_after(start, shape, swapped[i].reversed, scene.grid);
                    if (!placed)
                        continue;
                    object->symbol = placed->object.symbol;
                    object->col = placed->object.col;
                    object->row = placed->object.row;
                    object->rotation = placed->object.rotation;
                    object->base_height = placed->object.base_height;
                    (void)load_object_model(scene, b.models, assets, *object);
                    piece.symbol = shape.record.name + std::string(kSectionTag);
                    piece.tile_x = object->col;
                    piece.tile_z = object->row;
                    piece.height = object->base_height.value_or(0);
                    piece.direction = static_cast<std::uint8_t>(object->rotation);
                    piece.reversed = swapped[i].reversed;
                }
                objects_changed(scene);
            }
            // shortcut: the simulation keeps running the trains on the course the park loaded with, upgrade when
            // a changed track is taken into the running park.
            if (*b.ride < scene.courses.size())
                scene.courses[*b.ride] = park::track_course(ride, scene.objects, scene.models, scene.grid, scene.fitted, [&](const save::TrackPiece& p, const save::PlacedObject&) { return park::shape_named(*c, p.symbol); });
            update_platforms(scene, *b.ride, assets);
        }

        /// Adds the ghost to the park and to the ride's track, paid for if the park has the money, or `forced`
        /// whether or not it has.
        void build(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, bool forced = false)
        {
            const park::PieceCatalogue* c = catalogue(b);
            save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (c == nullptr || !b.ghost || b.off_water || !b.picked || !ride.track)
                return;
            wait_for_build(scene);
            save::PlacedObject object = b.ghost->object;
            object.id = last_id(scene) + 1;
            object.owner_ref = object.id;
            const auto objects = object_indices(scene);
            save::TrackPiece piece;
            if (!ride.track->pieces.empty())
            {
                const save::TrackPiece& before = ride.track->pieces.back();
                piece.colours = before.colours;
                piece.segment = ride.track->segments.empty() ? before.segment : static_cast<std::uint32_t>(ride.track->segments.size() - 1);
                piece.asset = before.asset;
                if (const auto found = objects.find(before.scenery); found != objects.end())
                    object.colours = scene.objects[found->second].colours;
            }
            const park::PieceShape& shape = c->pieces[b.picked->index];
            if (scene.park.cash)
            {
                if (*scene.park.cash < shape.record.cost && !forced)
                    return;
                *scene.park.cash -= shape.record.cost;
            }
            // Its end meeting where the first piece starts closes the track.
            if (!opening(ride) && !b.backward)
            {
                const save::TrackPiece& first = stretch_start(*ride.track);
                const auto first_object = objects.find(first.scenery);
                const auto first_shape = park::shape_named(*c, first.symbol);
                if (first_object != objects.end() && first_shape)
                {
                    const park::TrackTip start = park::start_of(scene.objects[first_object->second], *first_shape, first.reversed, scene.grid);
                    const bool closes = glm::length(b.ghost->next.position - start.position) < kCloses && b.ghost->next.end == start.end;
                    if (!ride.track->segments.empty())
                        ride.track->segments.back().closed = closes;
                    ride.track->circuit = closes && std::ranges::all_of(ride.track->segments, &save::TrackSegment::closed);
                }
            }
            piece.id = object.id;
            piece.symbol = shape.record.name + std::string(kSectionTag);
            piece.scenery = object.id;
            piece.tile_x = object.col;
            piece.tile_z = object.row;
            piece.height = object.base_height.value_or(0);
            piece.direction = static_cast<std::uint8_t>(object.rotation);
            piece.reversed = b.picked->reversed != b.backward;
            ride.track->pieces.insert(b.backward ? ride.track->pieces.begin() : ride.track->pieces.end(), piece);
            if (!ride.track->segments.empty())
                ++ride.track->segments.back().piece_count;
            scene.attractions[object.id] = { park::AttractionRef::Kind::Ride, *b.ride };
            scene.objects.push_back(std::move(object));
            objects_changed(scene);
            track_changed(scene, b, assets);
            b.tip = track_tip(scene, b);
            refresh(scene, b, assets);
        }

        /// Lays the pieces that bring the open end round to where the first piece starts, closing the track, or
        /// says why it cannot: no way found, too long looking, or not enough money for them all.
        void complete(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            const park::PieceCatalogue* c = catalogue(b);
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            const auto last = last_piece(scene, b);
            if (c == nullptr || !b.tip || !last || !ride.track || b.backward)
                return;
            const save::TrackPiece& first = stretch_start(*ride.track);
            const auto objects = object_indices(scene);
            const auto object = objects.find(first.scenery);
            const auto shape = park::shape_named(*c, first.symbol);
            if (object == objects.end() || !shape)
                return;
            const park::LaidPiece start { static_cast<std::size_t>(&*shape - c->pieces.data()), first.reversed };
            const auto way = park::complete_track(c->pieces, *b.tip, *last, park::start_of(scene.objects[object->second], *shape, first.reversed, scene.grid), start, scene.grid, [&](const save::PlacedObject& o) { return !off_water(scene, b, assets, o); }, kCompletionTries);
            if (!way)
            {
                b.complaint = way.error() == park::CompletionFailure::OutOfTries ? "RideAutoCompleteOutOfTime" : "RideAutoCompleteNoSolution";
                return;
            }
            std::int64_t cost = 0;
            for (const park::LaidPlacement& p : *way)
                cost += c->pieces[p.piece.index].record.cost;
            if (scene.park.cash && *scene.park.cash < cost)
            {
                b.complaint = "RideAutoCompleteNotEnoughCash";
                return;
            }
            logging::logger("park").info("track finished with {} pieces", way->size());
            for (const park::LaidPlacement& p : *way)
            {
                const park::PieceShape& laid = c->pieces[p.piece.index];
                save::PlacedObject o = p.object;
                o.overlay = b.overlay;
                if (!load_object_model(scene, b.models, assets, o))
                    return;
                b.picked = p.piece;
                b.ghost = park::PiecePlacement { o, park::tip_of(o, laid, p.piece.reversed, scene.grid) };
                b.off_water = false; // The way was found on water
                build(scene, b, assets);
            }
        }

        /// Takes the last piece laid out of the park and off the ride's track, which opens a closed circuit.
        void demolish(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (!ride.track || ride.track->pieces.empty())
                return;
            // A stretch begun but not yet laid is given up instead.
            if (opening(ride))
            {
                ride.track->segments.pop_back();
                b.tip = track_tip(scene, b);
                refresh(scene, b, assets);
                return;
            }
            wait_for_build(scene);
            // Built backwards, the first piece is the one laid last.
            const bool front = b.backward && ride.track->pieces.size() > 1;
            const save::TrackPiece last = front ? ride.track->pieces.front() : ride.track->pieces.back();
            ride.track->pieces.erase(front ? ride.track->pieces.begin() : ride.track->pieces.end() - 1);
            ride.track->circuit = false;
            if (!ride.track->segments.empty() && ride.track->segments.back().piece_count > 0)
                --ride.track->segments.back().piece_count;
            // A stretch emptied gives way to the one before, open at its end again.
            if (ride.track->segments.size() > 1 && ride.track->segments.back().piece_count == 0)
                ride.track->segments.pop_back();
            if (!ride.track->segments.empty())
                ride.track->segments.back().closed = false;
            const auto objects = object_indices(scene);
            if (const auto found = objects.find(last.scenery); found != objects.end())
                scene.objects[found->second].hidden = true;
            for (save::PlacedObject& o : scene.objects)
                if (last.platform && o.owner_ref == last.platform->id && o.id != last.platform->id)
                    o.hidden = true;
            scene.attractions.erase(last.scenery);
            objects_changed(scene);
            track_changed(scene, b, assets);
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
        // Adds `file` and the overlays it takes assets from to the index.
        const auto add_with_references = [&](const std::filesystem::path& file) {
            const bool fresh = m.index->add_file(file);
            std::set<std::string, std::less<>> tried;
            for (auto missing = m.index->missing_references(); !missing.empty(); missing = m.index->missing_references())
            {
                bool more = false;
                for (const std::string& reference : missing)
                {
                    if (!tried.insert(reference).second)
                        continue;
                    if (const auto found_file = assets.install->find(std::format("{}.common.ovl", reference)))
                        more = m.index->add_file(*found_file) || more;
                }
                if (!more)
                    break;
            }
            return fresh;
        };
        if (added)
        {
            // Only the overlay and those it takes assets from: scanning the whole track folder would be slow.
            m.index = std::make_unique<park::AssetIndex>(park::AssetIndex::scan(assets.install->root(), { }));
            if (const auto file = assets.install->find(std::format("{}.common.ovl", slashes(object.overlay))))
                (void)add_with_references(*file);
            m.loader = std::make_unique<park::ModelLoader>(*m.index);
        }
        if (m.missing.contains(object.symbol))
            return false;
        auto model = m.loader->load(object.symbol, object.overlay);
        if (!model)
        {
            // Path and queue styles and support sets keep each piece in an overlay of its own, named after the
            // piece, which the describing overlay does not refer to: beside it, or in a folder beside it.
            const std::string overlay = slashes(object.overlay);
            const std::string folder = overlay.substr(0, overlay.rfind('/') + 1);
            const std::string file_name = std::format("{}.common.ovl", object.symbol);
            std::optional<std::filesystem::path> file = assets.install->find(folder + file_name);
            if (const auto dir = assets.install->find(folder.empty() ? "." : folder); !file && dir)
            {
                std::error_code error;
                for (const auto& entry : std::filesystem::directory_iterator(*dir, error))
                {
                    if (entry.is_directory(error))
                        file = assets.install->find(std::format("{}{}/{}", folder, entry.path().filename().string(), file_name));
                    if (file)
                        break;
                }
            }
            if (file && add_with_references(*file))
            {
                m.loader = std::make_unique<park::ModelLoader>(*m.index);
                model = m.loader->load(object.symbol, object.overlay);
            }
            // A theme keeps its rides and entrances in overlays of their own, named apart from the items they
            // hold, below its folder (Rides/<ride>/, EntranceExit/<style>/) or, for some, below the folder above
            // it (a sibling Rides/<theme>Rides/): the last resort reads all of those, each folder once.
            std::string up = folder;
            for (int level = 0; level < 2 && !model && !up.empty(); ++level, up = up.substr(0, up.rfind('/', up.size() - 2) + 1))
            {
                const auto dir = assets.install->find(up);
                if (!dir || !m.folders_read.insert(up).second)
                    continue;
                bool added = false;
                std::error_code error;
                for (auto it = std::filesystem::recursive_directory_iterator(*dir, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
                {
                    if (it->is_regular_file(error) && it->path().filename().string().ends_with(".common.ovl"))
                        added = add_with_references(it->path()) || added;
                }
                if (added)
                {
                    m.loader = std::make_unique<park::ModelLoader>(*m.index);
                    model = m.loader->load(object.symbol, object.overlay);
                }
            }
        }
        if (!model)
            m.missing.insert(object.symbol);
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
        scene.lamps = park::object_lamps(scene.objects, scene.models, scene.grid, scene.fitted);
        scene.lamps_due = true;
        show_park_particles(scene.particles, park::object_effects(scene.objects, scene.models, scene.grid, scene.fitted));
        scene.levels = { };
        scene.since_rebuild = kObjectRebuildInterval;
    }

    void wait_for_build(ParkScene& scene)
    {
        if (scene.object_build.valid())
            (void)scene.object_build.get();
    }

    namespace
    {
        /// The texture a ghost draws `batch` with. Ghosts are drawn see-through, which takes a picture's alpha
        /// as coverage; opaque parts may keep other things in their alpha (a path's surface does), so they get
        /// a copy of their own with it filled.
        std::string ghost_texture(const park::ObjectBatch& batch)
        {
            return batch.blend == park::PartBlend::Opaque ? std::format("ghost:{}", batch.texture) : batch.texture;
        }
    } // namespace

    void ghost_textures(render::Device& device, ParkScene& scene, const park::ObjectMesh& ghost)
    {
        std::vector<park::ObjectBatch> missing;
        for (const park::ObjectBatch& batch : ghost.batches)
        {
            if (!scene.object_textures.contains(ghost_texture(batch)))
                missing.push_back(batch);
        }
        if (missing.empty())
            return;
        auto pictures = park::paint_batches(missing, scene.pictures);
        for (const park::ObjectBatch& batch : missing)
        {
            const auto painted = pictures.find(batch.texture);
            if (batch.blend != park::PartBlend::Opaque || painted == pictures.end())
                continue;
            ovl::RgbaImage filled = painted->second;
            for (std::size_t i = 3; i < filled.pixels.size(); i += 4)
                filled.pixels[i] = 255;
            pictures.insert_or_assign(ghost_texture(batch), std::move(filled));
        }
        if (scene.relief_maps)
            park::add_relief_maps(pictures); // The pictures stay for the object once it is built
        std::erase_if(pictures, [&](const auto& entry) { return scene.object_textures.contains(entry.first); });
        auto uploaded = ui::upload_textures(device, pictures);
        if (uploaded)
            scene.object_textures.merge(*uploaded);
    }

    void close_track_building(ParkScene& scene)
    {
        scene.building.ride.reset();
        scene.building.ghost.reset();
        scene.building.ghost_mesh = { };
        if (auto panel = scene.desktop->root().find(kPanelName))
            panel->close();
        if (auto arm = scene.desktop->root().find(kArmName))
            arm->close();
        if (auto designs = scene.desktop->root().find(kDesignName))
            designs->close();
        if (auto placing = scene.desktop->root().find_as<ui::InfoPanel>(kPlacingName); placing && placing->showing() && !ride_placing(scene))
            placing->hide();
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

    bool fit_trains(ParkScene& scene, const config::GameInstall& install, std::size_t index, int trains, int cars)
    {
        save::Ride& ride = scene.rides.rides[index];
        if (ride.trains.empty() || trains < 1 || cars < 1)
            return false;
        const std::string symbol = ride.trains.front().symbol;
        const std::string path = ride.trains.front().overlay_path;
        const std::string_view name = std::string_view(symbol).substr(0, symbol.find(':'));
        park::AssetIndex found = park::AssetIndex::scan(install.root(), { });
        if (const auto file = install.find(std::format("{}.common.ovl", path)))
            (void)found.add_file(*file);
        park::ModelLoader loader(found);
        // The front car is the train's first design, the rest its second where it has one.
        const auto front = loader.load_car(name, path, 0);
        const auto rest = loader.load_car(name, path, 1);
        ride.trains.assign(static_cast<std::size_t>(trains), save::Train { });
        ride.seats.clear();
        std::vector<park::CarFit>& fits = scene.car_fits[index];
        fits.clear();
        bool arm = false;
        for (int t = 0; t < trains; ++t)
        {
            save::Train& train = ride.trains[static_cast<std::size_t>(t)];
            train.symbol = symbol;
            train.overlay_path = path;
            for (int car = 0; car < cars; ++car)
            {
                const auto& seated = car == 0 || !rest ? front : rest;
                arm = arm || (seated && !seated->arm_joints.empty());
                train.cars.push_back({ .index = car, .car_count = cars, .design_slot = car == 0 || !rest ? 0 : 1 });
                if (t == 0 && seated && seated->car_fit)
                    fits.push_back(*seated->car_fit);
                for (std::uint32_t seat = 0; seated && seat < seated->seats; ++seat)
                    ride.seats.push_back({ .car = car, .seat = static_cast<std::int32_t>(seat), .train = t });
            }
        }
        ride.train_count = trains;
        ride.cars_per_train = cars;
        return arm;
    }

    bool new_tracked_ride(ParkScene& scene, const park::BuildItem& item, const ui::InterfaceAssets& assets)
    {
        if (item.kind != park::BuildKind::TrackedRide || !assets.install)
            return false;
        const auto file = assets.install->find(std::format("{}.common.ovl", item.overlay));
        if (!file)
            return false;
        const auto overlay = ovl::Overlay::load(*file);
        if (!overlay)
            return false;
        // The first overlay the ride's file names that holds pieces.
        std::optional<std::string> pieces;
        for (std::string named : park::named_overlays(*overlay, item.overlay))
        {
            if (scene.building.catalogues.contains(named))
                pieces = std::move(named);
            else if (const auto found = assets.install->find(std::format("{}.common.ovl", named)))
            {
                if (auto loaded = park::load_piece_catalogue(*found, named); loaded && !loaded->pieces.empty())
                {
                    scene.building.catalogues.emplace(named, std::move(*loaded));
                    pieces = std::move(named);
                }
            }
            if (pieces)
                break;
        }
        if (!pieces)
        {
            logging::logger("park").warning("no track pieces for {}", item.overlay);
            return false;
        }
        const std::size_t index = scene.rides.rides.size();
        save::Ride& ride = scene.rides.rides.emplace_back();
        ride.kind = save::RideKind::Tracked;
        ride.base.id = last_id(scene) + 1;
        ride.base.symbol = std::format("{}:trr", item.symbol);
        ride.base.overlay_path = item.overlay;
        // Its kind's name and a number, as the game names a new ride.
        ride.base.name = park::numbered_name(scene.rides.rides, ride.base.symbol);
        // Built closed, as in the game: the player tests it and opens it from its window.
        ride.base.state = save::AttractionState::Closed;
        ride.track.emplace().segments.resize(1);
        // One train of the first the ride's file lists, seated as its car models are; its window changes them
        // until it opens.
        std::string_view train_name;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
            for (const ovl::TrackedRideItem& t : part ? ovl::read_tracked_rides(*overlay, *part) : std::vector<ovl::TrackedRideItem> { })
                if (strings::equals_ignoring_case(t.name, item.symbol) && t.ride && t.ride->operating)
                {
                    if (!t.ride->operating->trains.empty())
                        train_name = t.ride->operating->trains.front();
                    const ovl::RideOperatingDefaults& o = *t.ride->operating;
                    ride.platform_type = o.platform;
                    ride.mode = static_cast<save::OperatingMode>(o.mode);
                    // Its speeds as its file sets them, which the station pushes and the lift pulls trains at.
                    ride.options.launch_speed = o.launch_speed;
                    ride.options.lift_speed = o.lift_speed;
                    ride.options.brake_speed = o.brake_speed;
                    ride.options.block_brake_off_speed = o.block_brake_speed;
                    ride.options.fixed_speed = o.fixed_speed.value_or(-1);
                }
        // Its platforms come from the folder of tracks/Platforms that has its type.
        std::error_code error;
        const auto platform_folders = assets.install->find("Tracks/Platforms");
        for (auto it = std::filesystem::directory_iterator(platform_folders.value_or(std::filesystem::path { }), error); !ride.platform_type.empty() && !error && it != std::filesystem::directory_iterator(); it.increment(error))
            if (it->is_directory(error) && assets.install->find(std::format("Tracks/Platforms/{}/{}.common.ovl", it->path().filename().string(), ride.platform_type)))
                ride.platform_style = it->path().filename().string();
        if (ride.platform_style.empty())
            ride.platform_type.clear();
        const std::filesystem::path cars = assets.install->find("Cars").value_or(std::filesystem::path { });
        bool arm = false; // Its riders sit on arms
        for (auto it = std::filesystem::recursive_directory_iterator(cars, error); !train_name.empty() && !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        {
            if (!strings::equals_ignoring_case(it->path().filename().string(), std::format("{}.common.ovl", train_name)))
                continue;
            std::string path = std::filesystem::relative(it->path(), assets.install->root()).generic_string();
            path.resize(path.size() - std::string_view(".common.ovl").size());
            save::Train& train = ride.trains.emplace_back();
            train.symbol = std::format("{}:rit", train_name);
            train.overlay_path = path;
            // As many cars as its kind usually has, and the fewest and most it may have.
            ui::TrainLimits& limits = scene.train_limits[index];
            const auto car_file = ovl::Overlay::load(it->path());
            for (const auto part : car_file ? std::array { maybe(&car_file->common()), car_file->unique() } : std::array<std::optional<const ovl::Part&>, 2> { })
                for (const ovl::RideTrainItem& t : part ? ovl::read_ride_trains(*car_file, *part) : std::vector<ovl::RideTrainItem> { })
                    if (t.train && strings::equals_ignoring_case(t.name, train_name) && t.train->fewest_cars > 0 && t.train->fewest_cars <= t.train->most_cars)
                        limits = { .fewest_cars = static_cast<int>(t.train->fewest_cars), .most_cars = static_cast<int>(t.train->most_cars), .usual_cars = static_cast<int>(std::clamp(t.train->usual_cars, t.train->fewest_cars, t.train->most_cars)) };
            arm = fit_trains(scene, *assets.install, index, 1, limits.usual_cars);
            logging::logger("park").info("new {} runs {} with {} seats, lift {} m/s", item.symbol, train_name, ride.seats.size(), ride.options.lift_speed);
            break;
        }
        // The simulation takes it on once its entrance and exit are placed beside its station (ride_placing.hpp).
        scene.track_rules.resize(scene.rides.rides.size());
        scene.track_rules[index] = park::load_track_rules(*file, ride.base.symbol);
        scene.courses.resize(scene.rides.rides.size());
        logging::logger("park").info("new {} built from {}", item.symbol, *pieces);
        // The saved designs of its kind, offered until its first piece is laid: the install's, then the player's
        // own in our folder and in the game's.
        // shortcut: none locked, as none the install ships is; upgrade with designs a scenario locks.
        std::vector<std::filesystem::path> designs;
        std::vector<std::filesystem::path> folders { assets.install->find(kDesignFolder).value_or(std::filesystem::path { }) };
        if (scene.saves.data)
            folders.push_back(scene.saves.data->folder(config::UserFolder::Coasters));
        if (const auto name = config::game_saves_folder(config::UserFolder::Coasters); scene.saves.game_saves && name)
            folders.push_back(*scene.saves.game_saves / *name);
        for (const std::filesystem::path& folder : folders)
        {
            const std::size_t first = designs.size();
            error.clear();
            for (auto it = std::filesystem::directory_iterator(folder, error); !error && it != std::filesystem::directory_iterator(); it.increment(error))
            {
                if (!strings::equals_ignoring_case(it->path().extension().string(), kDesignExtension))
                    continue;
                if (const auto design = design_ride(it->path()); design && strings::equals_ignoring_case(std::string_view(design->ride.base.symbol).substr(0, design->ride.base.symbol.find(':')), item.symbol))
                    designs.push_back(it->path());
            }
            std::ranges::sort(designs.begin() + static_cast<std::ptrdiff_t>(first), designs.end());
        }
        const bool opened = open_track_building(scene, index, *pieces, assets);
        scene.building.arm = scene.building.arm || arm;
        scene.building.designs = std::move(designs);
        return opened;
    }

    void update_platforms(ParkScene& scene, std::size_t ride_index, const ui::InterfaceAssets& assets)
    {
        save::Ride& ride = scene.rides.rides.at(ride_index);
        const auto overlay = ride_track_overlay(scene, ride_index);
        const auto c = overlay ? load_catalogue(scene.building, *overlay, assets) : std::nullopt;
        if (!c || !ride.track || ride.platform_type.empty() || !assets.install)
            return;
        // The platform pieces its type's file holds, by name in any case: types are not spelt as their pieces.
        const std::string platforms = std::format("Tracks/Platforms/{}/{}", ride.platform_style, ride.platform_type);
        const auto file = assets.install->find(std::format("{}.common.ovl", platforms));
        if (!file)
            return;
        const auto loaded = ovl::Overlay::load(*file);
        if (!loaded)
            return;
        std::vector<std::string> names;
        for (const auto part : { maybe(&loaded->common()), loaded->unique() })
            for (const auto& symbol : part ? part->symbols : std::vector<ovl::Symbol> { })
                if (const ovl::SymbolName n = ovl::split_symbol_name(symbol.name); n.tag == "sid")
                    names.emplace_back(n.name);
        std::vector<park::DoorSpot> doors;
        for (const save::Station& s : ride.stations)
            for (const auto& spot : { s.entrance_spot, s.exit_spot })
                if (spot)
                    doors.push_back({ spot->col, spot->row, spot->direction, spot->height });
        auto& pieces = ride.track->pieces;
        const auto station = [&](std::size_t i) {
            const auto shape = park::shape_named(*c, pieces[i].symbol);
            return pieces[i].platform.has_value() || (shape && (shape->record.flags & park::kPieceStation) != 0);
        };
        wait_for_build(scene);
        bool changed = false;
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
            if (!station(i))
                continue;
            const auto shape = park::shape_named(*c, pieces[i].symbol);
            const bool alone = shape && (shape->record.flags & park::kPiecePlatformAlone) != 0;
            const bool before = !alone && (i > 0 ? station(i - 1) : ride.track->circuit && station(pieces.size() - 1));
            const bool after = !alone && (i + 1 < pieces.size() ? station(i + 1) : ride.track->circuit && station(0));
            const std::string want = park::platform_symbol(ride.platform_type, pieces[i], before, after, doors);
            const auto name = std::ranges::find_if(names, [&](const std::string& n) { return strings::equals_ignoring_case(n, want); });
            if (name == names.end())
                continue;
            const auto objects = object_indices(scene);
            const auto piece_object = objects.find(pieces[i].scenery);
            auto platform = pieces[i].platform ? std::ranges::find_if(scene.objects, [&](const save::PlacedObject& o) { return o.owner_ref == pieces[i].platform->id && o.id != pieces[i].platform->id; }) : scene.objects.end();
            if (platform == scene.objects.end())
            {
                if (piece_object == objects.end())
                    continue;
                // Stands where its piece does.
                save::PlacedObject o = scene.objects[piece_object->second];
                const std::uint64_t id = last_id(scene) + 1;
                pieces[i].platform = save::TrackPlatform { .id = id, .tile_x = pieces[i].tile_x, .tile_z = pieces[i].tile_z, .height = pieces[i].height, .direction = pieces[i].direction };
                o.id = id + 1;
                o.owner_ref = id;
                o.overlay = platforms;
                o.symbol.clear();
                scene.objects.push_back(std::move(o));
                platform = scene.objects.end() - 1;
            }
            if (platform->symbol == *name && !platform->hidden)
                continue;
            platform->symbol = *name;
            platform->hidden = false;
            (void)load_object_model(scene, scene.building.models, assets, *platform);
            changed = true;
        }
        if (changed)
            objects_changed(scene);
    }

    bool track_building_click(ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        TrackBuilding& b = scene.building;
        if (!b.ride || !b.ghost)
            return false;
        save::Ride& ride = scene.rides.rides.at(*b.ride);
        const park::PieceCatalogue* c = catalogue(b);
        if (!opening(ride) || c == nullptr)
            return false;
        // A design is laid whole, piece after piece from the first, and paid for whatever the park has, then
        // its doors where they stood beside its stations. Its price is its pieces': the only scenery designs
        // keep is the style's standard entrance and exit, which their files price at nothing.
        const std::vector<save::TrackPiece> design = std::exchange(b.design, { });
        const std::vector<TrackBuilding::DesignDoor> doors = std::exchange(b.design_doors, { });
        const std::optional<save::Ride> settings = std::exchange(b.design_ride, std::nullopt);
        const std::int64_t cash = scene.park.cash.value_or(0);
        for (std::size_t i = 0; i < std::max<std::size_t>(design.size(), 1); ++i)
        {
            if (i > 0)
            {
                const auto shape = park::shape_named(*c, design[i].symbol);
                auto placed = shape && b.tip ? park::place_after(*b.tip, *shape, design[i].reversed, scene.grid) : std::nullopt;
                if (!placed)
                    break;
                placed->object.overlay = b.overlay;
                if (!load_object_model(scene, b.models, assets, placed->object))
                    break;
                b.picked = park::LaidPiece { static_cast<std::size_t>(&*shape - c->pieces.data()), design[i].reversed };
                b.ghost = std::move(*placed);
            }
            const std::size_t laid = ride.track->pieces.size();
            build(scene, b, assets, !design.empty());
            if (ride.track->pieces.size() == laid)
                break;
            if (!design.empty())
            {
                save::TrackPiece& piece = ride.track->pieces.back();
                piece.arm_angles = design[i].arm_angles;
                piece.brake_speed = design[i].brake_speed;
                piece.holding_time = design[i].holding_time;
            }
        }
        if (!design.empty() && ride.track->pieces.size() == design.size())
        {
            // It runs as the design was set to run, in the design's colours.
            if (settings)
            {
                // Its trains as many and as long as the design's, as far as its kind and its track allow.
                if (const auto limits = scene.train_limits.find(*b.ride); limits != scene.train_limits.end() && settings->train_count > 0 && settings->cars_per_train > 0)
                {
                    const int cars = std::clamp(settings->cars_per_train, limits->second.fewest_cars, limits->second.most_cars);
                    b.arm = fit_trains(scene, *assets.install, *b.ride, 1, cars) || b.arm;
                    const auto& course = scene.courses[*b.ride];
                    const int trains = std::min(settings->train_count, course ? park::trains_fitting(*course, scene.car_fits[*b.ride]) : 1);
                    if (trains > 1)
                        fit_trains(scene, *assets.install, *b.ride, trains, cars);
                }
                ride.mode = settings->mode;
                ride.options = settings->options;
                ride.car_colours = settings->car_colours;
                ride.fence_colours = settings->fence_colours;
                ride.queue_colours = settings->queue_colours;
            }
            // Moved and turned as the first piece was.
            const save::TrackPiece& from = design.front();
            const save::TrackPiece& to = ride.track->pieces.front();
            const int turns = (to.direction - from.direction + 4) % 4;
            for (const TrackBuilding::DesignDoor& door : doors)
            {
                save::PlacedObject o = door.object;
                std::int32_t across = o.col - from.tile_x;
                std::int32_t along = o.row - from.tile_z;
                for (int t = 0; t < turns; ++t)
                    std::tie(across, along) = std::pair { along, -across };
                o.col = to.tile_x + across;
                o.row = to.tile_z + along;
                o.rotation = (o.rotation + turns) % 4;
                o.base_height = o.base_height.value_or(0) - from.height + to.height;
                o.force_absolute_height = true;
                if (load_object_model(scene, b.models, assets, o))
                    place_station_door(scene, assets, *b.ride, door.station, o, *o.base_height, door.exit);
            }
        }
        if (!design.empty())
            logging::logger("park").info("design laid with {} of {} pieces for {}, mode {}, lift {} m/s", ride.track->pieces.size(), design.size(), cash - scene.park.cash.value_or(0), std::to_underlying(ride.mode), ride.options.lift_speed);
        // A ride that is one building is done once set down: on to its window, where its doors are placed.
        if (*b.ride < scene.track_rules.size() && scene.track_rules[*b.ride].one_piece && !ride.track->pieces.empty())
        {
            scene.info_requested = park::AttractionRef { park::AttractionRef::Kind::Ride, *b.ride };
            close_track_building(scene);
        }
        return true;
    }

    bool open_track_building(ParkScene& scene, std::size_t ride, std::string overlay, const ui::InterfaceAssets& assets)
    {
        if (ride >= scene.rides.rides.size() || !scene.rides.rides[ride].track || !assets.install)
            return false;
        TrackBuilding& b = scene.building;
        if (!load_catalogue(b, overlay, assets))
            return false;
        // Golf paths are never banked, and their pieces list the path's edges either way round, which reads as
        // upside down against the station.
        if (scene.rides.rides[ride].mode == save::OperatingMode::MiniGolf)
            for (park::PieceShape& p : b.catalogues.at(overlay).pieces)
                p.entry.bank = p.exit.bank = 0;
        b.ride = ride;
        b.overlay = std::move(overlay);
        b.choice = { };
        b.aim.reset();
        b.turns = 0;
        b.tip = track_tip(scene, b);
        b.arm = std::ranges::any_of(scene.arm_cars, [&](const ParkScene::ArmCar& a) { return scene.car_spots[a.car].ride == ride; });
        b.arm_piece = 0;
        b.arm_back = 0;
        b.designs.clear();
        b.design.clear();
        b.design_ride.reset();
        refresh(scene, b, assets);
        if (!scene.desktop->root().find(kPanelName))
        {
            auto& panel = scene.desktop->root().add<ui::ConstructionPanel>(std::string(kPanelName), ui::game_skins::construction_panel_style(assets.palette), std::u16string(assets.strings->get(kChainHelp)), std::u16string(assets.strings->get(kDemolishHelp)));
            panel.complete_button().tooltip = assets.strings->get(kCompleteHelp);
            panel.new_segment_button().tooltip = assets.strings->get(kNewSegmentHelp);
            panel.placement = ui::bottom_left(0, kPanelBottom, ui::construction_panel::kWidth, ui::construction_panel::kHeight);
        }
        // A ride that is one building is set down whole, as a flat ride is: the round panel with its turn button
        // stands in for the construction panel, which keeps the building's state out of sight.
        const bool whole = ride < scene.track_rules.size() && scene.track_rules[ride].one_piece;
        scene.desktop->root().find(kPanelName)->visible = !whole;
        if (auto placing = scene.desktop->root().find_as<ui::InfoPanel>(kPlacingName))
        {
            // shortcut: the round view stays empty, upgrade by drawing the ride's one piece in it.
            std::vector<ui::InfoTab> tabs(1);
            tabs[0].icon = "IconAttractionEditorRotate";
            tabs[0].help = assets.strings->get("ATE_TT_Rotate");
            tabs[0].action = true;
            if (whole)
                placing->show(std::move(tabs), 1);
            else if (placing->showing())
                placing->hide();
        }
        return true;
    }

    namespace
    {
        /// The strip above the panel that sets the arm angles of a piece of the ride, for a ride whose riders
        /// sit on arms: buttons that step to the piece before or after (the last piece at first), a tick that
        /// gives the piece angles of its own, buttons that zero them or copy those of the nearest piece before
        /// or after that has some, and a spinner for each joint.
        void update_arm_strip(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            namespace skins = ui::game_skins;
            auto& pieces = scene.rides.rides.at(*b.ride).track->pieces;
            auto strip = scene.desktop->root().find_as<ui::Panel>(kArmName);
            if (!b.arm || pieces.empty())
            {
                if (strip)
                    strip->close();
                return;
            }
            constexpr float kRow = 24;
            constexpr float kSpinner = 64;
            if (!strip)
            {
                strip = scene.desktop->root().add<ui::Panel>(std::string(kArmName), std::string(skins::kCurvedStrip));
                strip->placement = ui::bottom_left(ui::construction_panel::kStripLeft, kPanelBottom + ui::construction_panel::kHeight, 540, kRow + 8);
                auto& enable = strip->add<ui::Tickbox>("enable", std::string(skins::kTickbox), std::string(skins::kTick));
                enable.tooltip = assets.strings->get(kArmEnableHelp);
                enable.placement = ui::top_left(8, 6, 20, 20);
                auto& label = strip->add<ui::Label>("enable_label", std::u16string(assets.strings->get(kArmEnable)));
                label.placement = ui::top_left(30, 6, 60, 20);
                float left = 92;
                for (const auto& [name, text, help] : { std::tuple { "zero", u"0", kArmZeroHelp }, { "copy_prev", u"<", kArmPrevHelp }, { "copy_next", u">", kArmNextHelp } })
                {
                    auto& button = strip->add<ui::Button>(name, std::string(skins::kTextButton));
                    button.text = text;
                    button.tooltip = assets.strings->get(help);
                    button.placement = ui::top_left(left, 5, 22, 22);
                    left += 24;
                }
                for (const std::string_view joint : kArmJoints)
                {
                    auto& spinner = strip->add<ui::Spinner>(std::string(joint), skins::spinner_style(assets.palette));
                    spinner.minimum = -kArmTurns;
                    spinner.maximum = kArmTurns;
                    spinner.step = kArmStep;
                    spinner.tooltip = assets.strings->get(kArmAngleHelp);
                    spinner.placement = ui::top_left(left + 4, 5, kSpinner, 22);
                    left += kSpinner + 4;
                }
                for (const auto& [name, text, help] : { std::tuple { "piece_back", u"<<", kPieceBackHelp }, { "piece_forward", u">>", kPieceForwardHelp } })
                {
                    auto& button = strip->add<ui::Button>(name, std::string(skins::kTextButton));
                    button.text = text;
                    button.tooltip = assets.strings->get(help);
                    button.placement = ui::top_left(left + 4, 5, 26, 22);
                    left += 30;
                }
                auto& at = strip->add<ui::Label>("piece_label", std::u16string { });
                at.placement = ui::top_left(left + 6, 6, 60, 20);
                b.arm_piece = 0;
            }
            // Counted back from the last piece, so a piece laid while the last is shown is shown next.
            if (strip->find_as<ui::Button>("piece_back")->take_click())
                ++b.arm_back;
            if (strip->find_as<ui::Button>("piece_forward")->take_click() && b.arm_back > 0)
                --b.arm_back;
            b.arm_back = std::min(b.arm_back, pieces.size() - 1);
            const std::size_t index = pieces.size() - 1 - b.arm_back;
            save::TrackPiece& piece = pieces[index];
            strip->find_as<ui::Label>("piece_label")->text = strings::to_utf16(std::format("{}/{}", index + 1, pieces.size()));
            auto& enable = *strip->find_as<ui::Tickbox>("enable");
            // The nearest piece before or after this one (round a circuit) with angles of its own.
            const auto nearest = [&](bool before) -> std::optional<park::ArmAngles> {
                for (std::size_t k = 1; k < pieces.size(); ++k)
                    if (const auto& other = pieces[before ? (index + pieces.size() - k) % pieces.size() : (index + k) % pieces.size()]; other.arm_angles)
                        return other.arm_angles;
                return std::nullopt;
            };
            bool shown = b.arm_piece == piece.id;
            if (enable.take_change())
            {
                piece.arm_angles = enable.ticked ? nearest(true).value_or(park::ArmAngles { }) : std::optional<park::ArmAngles> { };
                shown = false;
            }
            for (const auto& [name, before] : { std::pair { "copy_prev", true }, { "copy_next", false } })
                if (strip->find_as<ui::Button>(name)->take_click())
                    if (const auto copied = nearest(before))
                    {
                        piece.arm_angles = copied;
                        shown = false;
                    }
            if (strip->find_as<ui::Button>("zero")->take_click())
            {
                piece.arm_angles = park::ArmAngles { };
                shown = false;
            }
            for (std::size_t j = 0; j < kArmJoints.size(); ++j)
            {
                auto& spinner = *strip->find_as<ui::Spinner>(kArmJoints[j]);
                if (spinner.take_change() && piece.arm_angles)
                    (*piece.arm_angles)[j] = static_cast<float>(spinner.value());
                spinner.enabled = piece.arm_angles.has_value();
                if (!shown)
                    spinner.set_value(piece.arm_angles ? static_cast<int>(std::lround((*piece.arm_angles)[j])) : 0);
            }
            enable.ticked = piece.arm_angles.has_value();
            b.arm_piece = piece.id;
        }
    } // namespace

    void update_track_building(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, int currency, std::optional<save::Tile> pointed, std::span<const ui::PlayCommand> commands)
    {
        TrackBuilding& b = scene.building;
        auto panel = scene.desktop->root().find_as<ui::ConstructionPanel>(kPanelName);
        if (!panel || !b.ride)
            return;
        auto placing = scene.desktop->root().find_as<ui::InfoPanel>(kPlacingName);
        const bool whole = !panel->visible && placing && placing->showing();
        if (panel->take_closed() || (whole && placing->take_closed()))
        {
            close_track_building(scene);
            return;
        }
        const park::PieceCatalogue* c = catalogue(b);
        bool changed = false;
        // Each press of the round panel's tab turns the ride a quarter, as a flat ride's does.
        if (whole && placing->take_action())
        {
            b.turns = (b.turns + 1) % 4;
            changed = true;
        }
        if (const save::Ride& ride = scene.rides.rides.at(*b.ride); opening(ride))
        {
            // The pointer off the ground, say over the panel, leaves the first piece where it was aimed.
            if (pointed && pointed != b.aim)
            {
                b.aim = pointed;
                changed = true;
            }
            for (const ui::PlayCommand command : commands)
            {
                if (command == ui::PlayCommand::TurnItemRight || command == ui::PlayCommand::TurnItemLeft)
                {
                    b.turns = (b.turns + (command == ui::PlayCommand::TurnItemRight ? 1 : 3)) % 4;
                    changed = true;
                }
            }
        }
        while (const auto event = panel->take_event())
        {
            if (c == nullptr)
                break;
            b.complaint.clear();
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
                case Complete:
                    complete(scene, b, assets);
                    break;
                case NewSegment:
                {
                    // shortcut: cars run the stretches as one course, jumping between them, upgrade when the course
                    // follows each stretch on its own.
                    save::Track& track = *scene.rides.rides.at(*b.ride).track;
                    track.segments.push_back({ .first_piece = track.pieces.size() });
                    b.design.clear();
                    b.design_ride.reset();
                    break;
                }
            }
            changed = true;
        }
        if (changed)
            refresh(scene, b, assets);
        ghost_textures(device, scene, b.ghost_mesh);
        panel->show(panel_view(scene, b, assets, currency));
        update_arm_strip(scene, b, assets);
        // The designs offered while the track has no pieces: custom track first, then each by its file's name
        // without the kind in brackets before it.
        auto designs = scene.desktop->root().find_as<ui::Dropdown>(kDesignName);
        if (b.designs.empty() || !scene.rides.rides.at(*b.ride).track || !scene.rides.rides.at(*b.ride).track->pieces.empty())
        {
            if (designs)
                designs->close();
            return;
        }
        if (!designs)
        {
            designs = scene.desktop->root().add<ui::Dropdown>(std::string(kDesignName), ui::game_skins::dropdown_style(assets.palette));
            designs->placement = ui::bottom_left(ui::construction_panel::kStripLeft + 8, kPanelBottom + ui::construction_panel::kHeight + 4, 260, 20);
            designs->items.emplace_back(assets.strings->get(kCustomTrack));
            for (const std::filesystem::path& file : b.designs)
            {
                const std::string stem = file.stem().string();
                designs->items.push_back(strings::to_utf16(std::string_view(stem).substr(stem.find(']') == std::string::npos ? 0 : stem.find(']') + 1)));
            }
            designs->select(0);
        }
        if (designs->take_change())
        {
            const std::size_t picked = designs->selected().value_or(0);
            const auto design = picked > 0 && picked <= b.designs.size() ? design_ride(b.designs[picked - 1]) : std::nullopt;
            b.design = design ? design->ride.track->pieces : std::vector<save::TrackPiece> { };
            b.design_ride = design ? std::optional(design->ride) : std::nullopt;
            b.design_doors.clear();
            for (std::size_t i = 0; design && i < design->ride.stations.size(); ++i)
            {
                for (const bool exit : { false, true })
                {
                    const std::uint64_t id = exit ? design->ride.stations[i].exit : design->ride.stations[i].entrance;
                    if (const auto door = std::ranges::find(design->scenery, id, &save::PlacedObject::id); id != 0 && door != design->scenery.end())
                        b.design_doors.push_back({ i, exit, *door });
                }
            }
            refresh(scene, b, assets);
        }
    }

    void draw_track_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.building.ride)
            draw_ghost_mesh(device, scene, scene.building.ghost_mesh, transform, light);
    }

    render::SceneLight ghost_light(render::SceneLight light)
    {
        constexpr float kGhostGlow = 2.4f; // Bright enough to pass the glow's threshold through the ghost's see-through tint
        light.sun = glm::vec3(0.0f);
        light.sky = glm::vec3(kGhostGlow);
        light.ground = glm::vec3(kGhostGlow);
        light.shadow_map = { };
        light.shadow_view = { };
        light.shadow_map_before = { };
        light.lamps = { };
        return light;
    }

    void draw_ghost_mesh(render::Device& device, const ParkScene& scene, const park::ObjectMesh& mesh, const glm::mat4& transform, const render::SceneLight& light, bool solid)
    {
        if (mesh.indices.empty() || !scene.see_through_pipeline)
            return;
        const auto vertices = device.allocate_transient(std::as_bytes(std::span(mesh.vertices)));
        const auto indices = device.allocate_transient(std::as_bytes(std::span(mesh.indices)));
        if (!vertices || !indices)
            return;
        for (const park::ObjectBatch& batch : mesh.batches)
        {
            const auto texture = scene.object_textures.find(ghost_texture(batch));
            render::DrawCommand draw;
            draw.pipeline = !solid || batch.blend == park::PartBlend::Blended ? scene.see_through_pipeline : (batch.blend == park::PartBlend::Cutout ? scene.cutout_pipeline : scene.object_pipeline);
            draw.vertices = *vertices;
            draw.indices = *indices;
            draw.index_type = render::IndexType::U32;
            draw.first = batch.first;
            draw.count = batch.count;
            draw.transform = transform;
            draw.texture = texture != scene.object_textures.end() ? texture->second.handle : scene.blank;
            draw.filter = render::Filter::Linear;
            draw.address = render::AddressMode::Repeat;
            draw.light = solid ? light : ghost_light(light);
            device.draw(draw);
        }
    }

    std::vector<save::TrackPiece> station_pieces(ParkScene& scene, std::size_t ride, const ui::InterfaceAssets& assets)
    {
        std::vector<save::TrackPiece> out;
        const auto overlay = ride_track_overlay(scene, ride);
        const auto c = overlay ? load_catalogue(scene.building, *overlay, assets) : std::nullopt;
        if (!c || !scene.rides.rides[ride].track)
            return out;
        for (const save::TrackPiece& piece : scene.rides.rides[ride].track->pieces)
        {
            const auto shape = park::shape_named(*c, piece.symbol);
            if (piece.platform || (shape && (shape->record.flags & park::kPieceStation) != 0))
                out.push_back(piece);
        }
        return out;
    }

} // namespace rct3::scene
