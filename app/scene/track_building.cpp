// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/track_building.hpp"

#include "career/career_file.hpp"
#include "config/game_saves.hpp"
#include "core/maybe.hpp"
#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "ovl/ride_train.hpp"
#include "ovl/tracked_ride.hpp"
#include "park/asset_index.hpp"
#include "park/flexi_colours.hpp"
#include "park/object_models.hpp"
#include "park/relief_maps.hpp"
#include "park/ride_placement.hpp"
#include "park/support_columns.hpp"
#include "park/track_completion.hpp"
#include "park/track_edit.hpp"
#include "park/track_rules.hpp"
#include "park/track_split.hpp"
#include "park/water_fill.hpp"
#include "profiling/profiler.hpp"
#include "save/model/design.hpp"
#include "save/save_file.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "scene/ride_placing.hpp"
#include "simulation/import_park.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/interface_assets.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/construction_panel.hpp"
#include "ui/screens/design_browser.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/screens/tool_row.hpp"
#include "ui/widgets/panel.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/tickbox.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rct3::scene
{

    TrackModels::TrackModels() = default;
    TrackModels::~TrackModels() = default;
    TrackModels::TrackModels(TrackModels&&) noexcept = default;
    TrackModels& TrackModels::operator=(TrackModels&&) noexcept = default;

    namespace
    {
        /// The building controls, in the strip of the round panel in the bottom-left corner.
        constexpr std::string_view kPanelName = "placing/body/construction";
        constexpr std::string_view kArmName = "construction_arm";   ///< The strip of arm angles above the panel
        constexpr std::string_view kBrowserName = "design_browser"; ///< The saved designs' window
        constexpr std::string_view kCustomTrack = "TrackBrowser_CustomTrack";
        constexpr std::string_view kLockedSuffix = "TrackBrowser_LockedSuffix";
        constexpr std::string_view kLockedHelp = "TrackBrowser_LockedHelp";
        constexpr std::string_view kDesignText = "TrackDesign_"; ///< Before a design's name, for its translated name
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
        /// In the order of park::PieceToggle.
        constexpr std::array<std::string_view, park::kPieceToggles> kToggleHelps { "ATE_TT_Chain", "ATE_TT_Enclosed", "ATE_TT_Water", "ATE_TT_Wide", "ATE_TT_Booster" };
        constexpr std::string_view kDemolishHelp = "ATE_TT_Demolish";
        constexpr std::string_view kCannotPlace = "IllegalTrackPlacement";
        constexpr std::string_view kCostLabel = "ShopItem_CostValue";
        /// The roles track files give pieces (ovl::TrackSection::role) that have a picture of their own in the
        /// construction panel, and the picture.
        struct RoleIcon
        {
            std::uint32_t role = 0;
            std::string_view skin;
        };
        constexpr std::uint32_t kBrakesRole = 1;
        constexpr std::uint32_t kHoldingRole = 24;
        constexpr std::array<std::uint32_t, 3> kBrakeRoles { kBrakesRole, 2, kHoldingRole }; ///< Brakes, block brakes, holding brakes
        constexpr int kMostHoldingTime = 30;                                                 ///< Seconds, as the game's box goes
        constexpr int kMostCarTurn = 720;                                                    ///< Degrees either way, as the game's box goes
        /// Metres a piece must end off to a side, or up or down, to be marked as going that way.
        constexpr float kMarkedRun = 0.5f;
        constexpr std::array<RoleIcon, 4> kRoleIcons { {
            { 3, "TrackSpecialIcon_StationMiddle" },
            { 4, "TrackSpecialIcon_PhotoSection" },
            { kBrakesRole, "TrackSpecialIcon_StraightBrakes" },
            { 2, "TrackSpecialIcon_StraightBlockBrakes" },
        } };
        constexpr float kMostBrakeSpeed = 30; ///< m/s, past the fastest coasters' top speeds
        constexpr std::string_view kCompleteHelp = "ATE_TT_Autocomplete";
        constexpr std::string_view kNewSegmentHelp = "ATE_TT_NewTrackSegment";
        constexpr std::string_view kSplitHelp = "ATE_TT_Split";
        /// Ends tried before finishing a track gives up, and each frame, a few seconds at most.
        constexpr std::size_t kCompletionTries = 20000;
        constexpr std::size_t kCompletionTriesPerFrame = 300;
        constexpr float kFlashSeconds = 0.4f; ///< The found way's button is lit, then dim, this long
        constexpr float kTestWait = 3;        ///< Seconds a test train stands at the open end
        /// How near a piece's end must come to where the first piece starts to close the track, in park units.
        constexpr float kCloses = 0.1f;
        constexpr std::string_view kPieceNames = "Track_"; ///< Followed by the section's name
        constexpr std::string_view kSectionTag = ":tks";
        /// Above the tool row, so the two never overlap.
        /// Of the round panel's top, from the screen's bottom edge: its disc stands as the path panel does.
        constexpr float kPanelTop = ui::game_skins::kOverToolRow + ui::info_panel::kDisc;
        /// The controls' room in the strip: from a little left of their rows to a little right of their pager,
        /// as high as the strip holds.
        constexpr float kControlsLeft = ui::construction_panel::kRowLeft - 8;
        constexpr float kControlsTop = 4;
        constexpr float kControlsWidth = ui::construction_panel::kListRight + 4 - kControlsLeft;
        constexpr float kControlsHeight = ui::construction_panel::kHeight - kControlsTop;
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

        /// The catalogue's piece a track piece is, laid as it is.
        std::optional<park::LaidPiece> laid_piece(const park::PieceCatalogue& c, const save::TrackPiece& piece)
        {
            const auto shape = park::shape_named(c, piece.symbol);
            return shape ? std::optional(park::LaidPiece { static_cast<std::size_t>(&*shape - c.pieces.data()), piece.reversed }) : std::nullopt;
        }

        /// A track piece as built: its object, of the scene's, and the catalogue's piece it is.
        struct BuiltPiece
        {
            std::size_t object = 0;
            park::LaidPiece laid;
        };

        std::optional<BuiltPiece> built_piece(const ParkScene& scene, const park::PieceCatalogue& c, const save::TrackPiece& piece)
        {
            const auto object = std::ranges::find(scene.objects, piece.scenery, &save::PlacedObject::id);
            const auto laid = laid_piece(c, piece);
            if (object == scene.objects.end() || !laid)
                return std::nullopt;
            return BuiltPiece { static_cast<std::size_t>(object - scene.objects.begin()), *laid };
        }

        /// Hides the platform built beside a station piece, which goes with it.
        void hide_platform(ParkScene& scene, const save::TrackPiece& piece)
        {
            for (save::PlacedObject& o : scene.objects)
                if (piece.platform && o.owner_ref == piece.platform->id && o.id != piece.platform->id)
                    o.hidden = true;
        }

        /// Builds on in `state`, at the open end of its track: the last stretch of that track still open, made the
        /// track's last.
        void use_split(ParkScene& scene, TrackBuilding& b, park::SplitState state)
        {
            b.split = state;
            const park::PieceCatalogue* c = catalogue(b);
            save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (c == nullptr || !ride.track || state == park::SplitState::None)
                return;
            const auto& segments = ride.track->segments;
            for (std::size_t s = segments.size(); s-- > 0;)
            {
                const auto end = segments[s].closed || segments[s].piece_count == 0 ? std::nullopt : park::shape_named(*c, ride.track->pieces.at(segments[s].first_piece + segments[s].piece_count - 1).symbol);
                if (end && end->record.side == park::split_side(state))
                {
                    park::make_last(*ride.track, s);
                    return;
                }
            }
        }

        /// Of the left and the right track of a ride of two, whether it is closed: none of its stretches is open.
        std::array<bool, 2> split_closed(const ParkScene& scene, const TrackBuilding& b)
        {
            std::array<bool, 2> closed { }, open { };
            const park::PieceCatalogue* c = catalogue(b);
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            for (const save::TrackSegment& s : c != nullptr && ride.track ? ride.track->segments : std::vector<save::TrackSegment> { })
            {
                const auto first = s.piece_count == 0 ? std::nullopt : park::shape_named(*c, ride.track->pieces.at(s.first_piece).symbol);
                if (first && (first->record.side == 1 || first->record.side == 2))
                    (s.closed ? closed : open).at(first->record.side - 1) = true;
            }
            return { closed[0] && !open[0], closed[1] && !open[1] };
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

        /// The file of the train named `train_name` among the install's cars.
        std::optional<std::filesystem::path> train_file(const config::GameInstall& install, std::string_view train_name)
        {
            std::error_code error;
            const std::filesystem::path cars = install.find("Cars").value_or(std::filesystem::path { });
            for (auto it = std::filesystem::recursive_directory_iterator(cars, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
                if (strings::equals_ignoring_case(it->path().filename().string(), std::format("{}.common.ovl", train_name)))
                    return it->path();
            return std::nullopt;
        }

        /// The name of a design's file without the kind in brackets before it ("Giga" of "[Giga]Giga.trk").
        std::string design_name(const std::filesystem::path& file)
        {
            const std::string stem = file.stem().string();
            const std::size_t close = stem.find(']');
            return stem.substr(close == std::string::npos ? 0 : close + 1);
        }

        /// Whether `o`, a piece of the ride being built, goes only on water and has dry land under it. Pieces of
        /// other rides may run under water, as rides that splash through it need.
        bool off_water(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, save::PlacedObject o)
        {
            o.overlay = b.overlay;
            if (!load_object_model(scene, b.models, assets, o))
                return false;
            const auto model = scene.models.find(park::model_ref(o));
            if (model == scene.models.end() || !model->second.water_only)
                return false;
            const park::SupportFootprint shape { .x_squares = model->second.squares[0], .z_squares = model->second.squares[1] };
            return !std::ranges::all_of(park::footprint_tiles(o, shape), [&](const auto& t) { return park::holds_water(scene.footpaths.water.bodies, { t.first, t.second }); });
        }

        /// The room the park's solid items take, but the pieces a piece `b` builds would join: the track's ends, or
        /// the piece built over and those either side of it. Joined pieces may share a tile, as diagonals do.
        /// shortcut: the track's far end is left out even where the new piece does not join it, so a piece may cross
        /// it; leave it in once a piece's ends tell which pieces it meets.
        park::SolidTiles taken_room(const ParkScene& scene, const TrackBuilding& b)
        {
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (!ride.track || ride.track->pieces.empty())
                return park::solid_tiles(scene.objects, scene.models, scene.grid);
            const auto& pieces = ride.track->pieces;
            std::vector<std::uint64_t> joined { pieces.front().scenery, pieces.back().scenery };
            if (b.selected)
            {
                joined.push_back(pieces.at(*b.selected).scenery);
                if (const auto around = park::piece_neighbours(*ride.track, *b.selected))
                    joined.insert(joined.end(), { pieces[around->before].scenery, pieces[around->after].scenery });
            }
            std::vector<save::PlacedObject> others;
            std::ranges::copy_if(scene.objects, std::back_inserter(others), [&](const save::PlacedObject& o) { return !std::ranges::contains(joined, o.id); });
            return park::solid_tiles(others, scene.models, scene.grid);
        }

        /// Whether `o`, its model loaded, would take room `taken` or a path takes.
        bool clashes(const ParkScene& scene, const park::SolidTiles& taken, const save::PlacedObject& o)
        {
            return park::clash(park::solid_tiles(std::span(&o, 1), scene.models, scene.grid), taken, scene.footpaths.paths, scene.grid) != park::Clash::None;
        }

        /// Recomputes what fits the open end, the piece picked and its ghost.
        void refresh(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            const profiling::Scope zone("Ghost");
            b.test.reset(); // A test runs the track as it was
            const park::PieceCatalogue* c = catalogue(b);
            b.candidates.clear();
            b.picked.reset();
            b.ghost.reset();
            b.ghost_mesh = { };
            b.off_water = false;
            b.clashes = false;
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            const bool starting = opening(ride);
            b.backward = false;
            const park::TrackRules rules = *b.ride < scene.track_rules.size() ? scene.track_rules[*b.ride] : park::TrackRules { };
            if (!starting)
                b.tip = track_tip(scene, b);
            // Over a piece picked between two others, what fits both, laid on from the end of the one before.
            std::optional<std::vector<park::LaidPiece>> between;
            if (const auto around = b.selected && c != nullptr && !starting ? park::piece_neighbours(*ride.track, *b.selected) : std::nullopt)
            {
                const auto before = built_piece(scene, *c, ride.track->pieces[around->before]);
                const auto after = built_piece(scene, *c, ride.track->pieces[around->after]);
                b.tip.reset();
                if (before && after)
                {
                    b.tip = park::tip_of(scene.objects[before->object], c->pieces[before->laid.index], before->laid.reversed, scene.grid);
                    const park::TrackTip goal = park::start_of(scene.objects[after->object], c->pieces[after->laid.index], after->laid.reversed, scene.grid);
                    between = park::pieces_between(c->pieces, *b.tip, before->laid, goal, after->laid, scene.grid);
                }
            }
            else
                b.selected.reset();
            if (c == nullptr || (starting ? !b.aim : !b.tip) || (!starting && rules.one_piece))
                return;
            b.candidates = between ? std::move(*between) : starting ? park::starting_pieces(c->pieces) : park::pieces_after(c->pieces, b.tip->end, last_piece(scene, b));
            // A ride built back from its station goes on from where its first piece starts: pieces laid backwards
            // from there, then kept in the track's own direction.
            // shortcut: the way follows the ride's file, never the player, upgrade with a forward and backward button.
            if (!starting && rules.builds_back && !b.selected)
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
            // Of side by side tracks, only joined pieces or only pieces alone, as the split button says.
            if (b.split != park::SplitState::None)
                std::erase_if(b.candidates, [&](const park::LaidPiece& l) {
                    const park::PieceRecord& r = c->pieces[l.index].record;
                    return park::split_joined(b.split) == r.partner.empty() || (starting && r.side != park::split_side(b.split));
                });
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
                if (const auto model = load_object_model(scene, b.models, assets, on) ? scene.models.find(park::model_ref(on)) : scene.models.end(); surface && model != scene.models.end() && model->second.water_only)
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
            b.clashes = clashes(scene, taken_room(scene, b), o);
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
            // Ghost track is held up as built track is, its supports ghosts too.
            if (const park::SupportSets* sets = support_sets(scene, assets))
            {
                for (save::PlacedObject& piece : supports_under(scene, shown, *sets))
                {
                    if (load_object_model(scene, b.models, assets, piece))
                        shown.push_back(std::move(piece));
                }
            }
            b.ghost_mesh = park::object_mesh(shown, scene.models, scene.grid);
            for (park::GroundVertex& v : b.ghost_mesh.vertices)
                v.colour *= b.off_water || b.clashes ? kRefusedTint : kGhostTint;
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

        /// A piece of the ride's list of pieces with a use of their own, and how it is laid where the track
        /// ends, when it can be.
        struct ListedPiece
        {
            ui::NamedPiece shown;
            std::optional<park::LaidPiece> laid;
        };

        /// Every piece of `c` with a use of its own, stations first, then in the file's order, once for each
        /// name and marks; those `o` offers where the track ends are offered.
        std::vector<ListedPiece> listed_pieces(const ui::StringTable& strings, const park::PieceCatalogue& c, const park::ChoiceOptions& o)
        {
            const auto shown_as = [&](const park::PieceShape& p) {
                ui::NamedPiece n { .name = piece_name(strings, p.record.name), .offered = false };
                // Its kind, as its name says.
                // shortcut: read from the section's name, upgrade if a ride names its pieces otherwise.
                std::string name = p.record.name;
                std::ranges::transform(name, name.begin(), strings::ascii_lower);
                const auto has = [&](std::initializer_list<std::string_view> words) { return std::ranges::any_of(words, [&](std::string_view w) { return name.contains(w); }); };
                if (const auto icon = std::ranges::find(kRoleIcons, p.record.role, &RoleIcon::role); icon != kRoleIcons.end())
                    n.icon = icon->skin;
                else if (has({ "tower" }))
                    n.icon = has({ "cap" }) ? "TrackSpecialIcon_VerticalCap" : has({ "vertical" }) ? "TrackSpecialIcon_VerticalMiddle" : "";
                n.marks[1] = std::ranges::contains(kBrakeRoles, p.record.role) || has({ "brake" }) ? "TrackSpecial_Block" : has({ "loop" }) ? "TrackSpecial_Loop" : has({ "helix" }) ? "TrackSpecial_Helix" : has({ "corkscrew", "roll", "twist" }) ? "TrackSpecial_Inverted" : has({ "curve", "turn", "bend" }) ? "TrackSpecial_Curve" : "TrackSpecial_Other";
                // Its way: off to a side, else up or down, from its start to its end as drawn. The side is level,
                // as the player sees it: an upside-down piece's rails swap sides.
                const glm::vec3 run = p.line.end - p.line.start;
                const glm::vec3 level = glm::cross(glm::vec3(0, 1, 0), p.line.start_direction);
                const float sideways = glm::dot(run, glm::length(level) > 0.1f ? glm::normalize(level) : p.line.start_side);
                n.marks[0] = sideways > kMarkedRun ? "TrackSpecial_Right" : sideways < -kMarkedRun ? "TrackSpecial_Left" : run.y > kMarkedRun ? "TrackSpecial_Up" : run.y < -kMarkedRun ? "TrackSpecial_Down" : "";
                return n;
            };
            const auto same = [](const ui::NamedPiece& a, const ui::NamedPiece& b) { return a.name == b.name && a.marks == b.marks; };
            std::vector<ListedPiece> list;
            for (const park::PieceUse use : { park::PieceUse::Station, park::PieceUse::Special })
            {
                for (const park::PieceShape& p : c.pieces)
                {
                    if (p.use != use)
                        continue;
                    ui::NamedPiece n = shown_as(p);
                    if (std::ranges::none_of(list, [&](const ListedPiece& l) { return same(l.shown, n); }))
                        list.push_back({ std::move(n), std::nullopt });
                }
            }
            for (const park::LaidPiece& laid : o.named)
            {
                const ui::NamedPiece n = shown_as(c.pieces[laid.index]);
                const auto found = std::ranges::find_if(list, [&](const ListedPiece& l) { return same(l.shown, n) && !l.laid; });
                if (found == list.end())
                    continue;
                found->laid = laid;
                found->shown.offered = true;
            }
            return list;
        }

        /// The panel's view of what the building offers now.
        ui::ConstructionView panel_view(const ParkScene& scene, const TrackBuilding& b, const ui::InterfaceAssets& assets, int currency, ui::DistanceUnits units)
        {
            ui::ConstructionView v;
            v.curves.fill(ui::IconState::Dis);
            v.slopes.fill(ui::IconState::Dis);
            v.banks.fill(ui::IconState::Dis);
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            v.can_demolish = ride.track && !ride.track->pieces.empty();
            v.can_complete = v.can_demolish && b.tip.has_value() && !b.backward && !opening(ride) && !b.selected;
            v.complete_dim = !b.proposed.empty() && std::fmod(b.clock, 2 * kFlashSeconds) >= kFlashSeconds;
            v.heights = b.heights;
            v.heights_locked = scene.show_ride_heights;
            const park::TrackRules rules = *b.ride < scene.track_rules.size() ? scene.track_rules[*b.ride] : park::TrackRules { };
            if (rules.car_facing)
                v.facing = b.facing;
            // A ride whose track may lie in several stretches, or a water transport, can begin another.
            if (rules.may_be_split || ride.mode == save::OperatingMode::WaterTransport)
                v.new_segment = v.can_demolish && !opening(ride) && !b.backward && !b.selected;
            if (b.split != park::SplitState::None)
                v.split = park::split_joined(b.split) ? 0 : park::split_side(b.split);
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
            for (std::size_t i = 0; i < park::kPieceToggles; ++i)
            {
                const std::uint8_t bit = park::toggle_bit(static_cast<park::PieceToggle>(i));
                if ((o.any_toggles & bit) != 0)
                    v.toggles.at(i) = state_of((b.choice.toggles & o.toggles & bit) != 0, (o.toggles & bit) != 0);
            }
            for (ListedPiece& l : listed_pieces(*assets.strings, *c, o))
            {
                if (l.laid && b.choice.named == l.laid)
                    v.named_on = v.named.size();
                v.named.push_back(std::move(l.shown));
            }
            if (b.picked)
            {
                const park::PieceShape& shape = c->pieces[b.picked->index];
                v.piece = piece_name(*assets.strings, shape.record.name);
                if (!b.ghost || b.clashes)
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
                // The settings the piece is built with. A brake's speed, in hundredths of the player's unit, is
                // the ride's: the last one set.
                // shortcut: nothing holds a train for the time or turns the cars yet, upgrade with holding brakes
                // and turning cars.
                using enum ui::PieceParam;
                if (shape.record.role == kBrakesRole)
                    v.params.at(std::to_underlying(Speed)) = ui::ParamBox { .value = static_cast<int>(std::lround(ride.options.brake_speed * ui::speed_scale(units) * 100)), .most = static_cast<int>(std::lround(kMostBrakeSpeed * ui::speed_scale(units) * 100)), .decimals = 2, .unit = std::u16string(ui::speed_unit(units)) };
                if (shape.record.role == kHoldingRole)
                    v.params.at(std::to_underlying(HoldingTime)) = ui::ParamBox { .value = b.holding_time, .most = kMostHoldingTime };
                if (rules.car_rotation)
                    v.params.at(std::to_underlying(CarTurn)) = ui::ParamBox { .value = b.car_turn, .least = -kMostCarTurn, .most = kMostCarTurn };
            }
            return v;
        }

        /// After a piece is built or taken away: swaps the pieces the ride's patterns match for the pieces they
        /// become, each laid where the one it replaces started, and measures the track's course again.
        void track_changed(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            const profiling::Scope zone("TrackChanged");
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

        /// The colours the next piece is built in: as painted, else as the last piece, else as the cars.
        save::RideColours next_colours(const save::Ride& ride, const TrackBuilding& b)
        {
            return b.colours.value_or(ride.track && !ride.track->pieces.empty() ? ride.track->pieces.back().colours : ride.car_colours);
        }

        /// Adds the ghost to the park and to the ride's track, paid for if the park has the money, or `forced`
        /// whether or not it has. Over a piece picked mid-track, the ghost takes its place, unless it is the
        /// same piece, which is left as it stands; either way the next piece along is picked.
        void build(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, bool forced = false)
        {
            const park::PieceCatalogue* c = catalogue(b);
            save::Ride& ride = scene.rides.rides.at(*b.ride);
            if (c == nullptr || !b.ghost || b.off_water || b.clashes || !b.picked || !ride.track)
                return;
            wait_for_build(scene);
            const auto over = b.selected ? built_piece(scene, *c, ride.track->pieces.at(*b.selected)) : std::nullopt;
            if (b.selected && !over)
                return;
            const auto step_on = [&] {
                const auto around = park::piece_neighbours(*ride.track, *b.selected);
                const std::size_t next = around ? (b.selected_back ? around->before : around->after) : 0;
                // Off the end of a stretch, building goes back to the open end.
                b.selected = around && park::piece_neighbours(*ride.track, next) ? std::optional(next) : std::nullopt;
                if (const auto laid = b.selected ? laid_piece(*c, ride.track->pieces[next]) : std::nullopt)
                    b.choice = park::choice_of(c->pieces, *laid);
                if (!b.selected)
                    use_split(scene, b, b.split);
                refresh(scene, b, assets);
            };
            if (over && over->laid == *b.picked)
            {
                step_on();
                return;
            }
            save::PlacedObject object = b.ghost->object;
            object.id = over ? scene.objects[over->object].id : last_id(scene) + 1;
            object.owner_ref = over ? scene.objects[over->object].owner_ref : object.id;
            const auto objects = object_indices(scene);
            save::TrackPiece piece;
            if (!ride.track->pieces.empty())
            {
                const save::TrackPiece& before = b.selected ? ride.track->pieces[*b.selected] : ride.track->pieces.back();
                piece.colours = before.colours;
                piece.segment = ride.track->segments.empty() || b.selected ? before.segment : static_cast<std::uint32_t>(ride.track->segments.size() - 1);
                piece.asset = before.asset;
                if (const auto found = objects.find(before.scenery); found != objects.end())
                    object.colours = scene.objects[found->second].colours;
            }
            else
            {
                // A new ride's track starts in its model's colours, as its cars do.
                piece.colours = ride.car_colours;
                object.colours = { ride.car_colours[0], ride.car_colours[1], ride.car_colours[2] };
            }
            if (b.colours)
            {
                piece.colours = *b.colours;
                object.colours = { (*b.colours)[0], (*b.colours)[1], (*b.colours)[2] };
            }
            const park::PieceShape& shape = c->pieces[b.picked->index];
            if (scene.park.cash)
            {
                if (*scene.park.cash < shape.record.cost && !forced)
                    return;
                *scene.park.cash -= shape.record.cost;
            }
            const bool closed_before = ride.track->circuit;
            // Its end meeting where the first piece starts closes the track.
            if (!opening(ride) && !b.backward && !b.selected)
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
            piece.id = b.selected ? ride.track->pieces[*b.selected].id : object.id;
            piece.symbol = shape.record.name + std::string(kSectionTag);
            piece.scenery = object.id;
            piece.tile_x = object.col;
            piece.tile_z = object.row;
            piece.height = object.base_height.value_or(0);
            piece.direction = static_cast<std::uint8_t>(object.rotation);
            piece.reversed = b.picked->reversed != b.backward;
            // shortcut: brake speeds are stored in whole metres a second as the files do, upgrade if a finer
            // speed matters once trains brake.
            if (shape.record.role == kBrakesRole)
                piece.brake_speed = static_cast<std::int32_t>(std::lround(ride.options.brake_speed));
            if (shape.record.role == kHoldingRole)
                piece.holding_time = b.holding_time;
            piece.car_turn = static_cast<float>(b.car_turn);
            piece.facing = b.facing;
            // The first piece down, the station, opens the ride's window, as in the game; a closed circuit goes on to
            // its doors.
            if (ride.track->pieces.empty())
                scene.info_requested = park::AttractionRef { park::AttractionRef::Kind::Ride, *b.ride };
            if (over)
            {
                // shortcut: the piece taken out is not paid back, as demolishing pays nothing back, upgrade when
                // pieces are sold back.
                save::TrackPiece& old = ride.track->pieces[*b.selected];
                hide_platform(scene, old); // A station's platform is built again if the new piece is one
                old = piece;
                scene.objects[over->object] = std::move(object);
                objects_changed(scene);
                track_changed(scene, b, assets);
                step_on();
                return;
            }
            ride.track->pieces.insert(b.backward ? ride.track->pieces.begin() : ride.track->pieces.end(), piece);
            if (!ride.track->segments.empty())
                ++ride.track->segments.back().piece_count;
            scene.attractions[object.id] = { park::AttractionRef::Kind::Ride, *b.ride };
            scene.objects.push_back(std::move(object));
            // A joined piece is laid with its other half on the other track: on after that track's open end where
            // it starts, else as a stretch of its own.
            // shortcut: demolishing or building over one half leaves the other as it is, upgrade by finding the
            // other half on the same tiles.
            if (const auto other = b.split != park::SplitState::None && !shape.record.partner.empty() ? park::shape_named(*c, shape.record.partner) : std::nullopt)
            {
                save::PlacedObject half = scene.objects.back();
                half.id = last_id(scene) + 1;
                half.owner_ref = half.id;
                half.symbol = other->record.item;
                save::TrackPiece joined = piece;
                joined.id = joined.scenery = half.id;
                joined.symbol = other->record.name + std::string(kSectionTag);
                const park::TrackTip start = park::start_of(half, *other, joined.reversed, scene.grid);
                const park::TrackTip end = park::tip_of(half, *other, joined.reversed, scene.grid);
                const auto meets = [](const park::TrackTip& a, const park::TrackTip& b) { return glm::length(a.position - b.position) < kCloses && a.end == b.end; };
                std::optional<std::size_t> onto;
                const auto& segments = ride.track->segments;
                for (std::size_t s = 0; !onto && s + 1 < segments.size(); ++s)
                {
                    const auto last = segments[s].closed || segments[s].piece_count == 0 ? std::nullopt : built_piece(scene, *c, ride.track->pieces.at(segments[s].first_piece + segments[s].piece_count - 1));
                    if (last && c->pieces[last->laid.index].record.side == other->record.side && meets(park::tip_of(scene.objects[last->object], c->pieces[last->laid.index], last->laid.reversed, scene.grid), start))
                        onto = s;
                }
                const std::size_t s = park::add_piece(*ride.track, onto, std::move(joined));
                // Its end meeting where its stretch starts closes that stretch.
                if (const auto first = segments[s].piece_count > 1 ? built_piece(scene, *c, ride.track->pieces.at(segments[s].first_piece)) : std::nullopt)
                    ride.track->segments[s].closed = meets(end, park::start_of(scene.objects[first->object], c->pieces[first->laid.index], first->laid.reversed, scene.grid));
                ride.track->circuit = std::ranges::all_of(ride.track->segments, &save::TrackSegment::closed);
                (void)load_object_model(scene, b.models, assets, half);
                scene.attractions[half.id] = { park::AttractionRef::Kind::Ride, *b.ride };
                scene.objects.push_back(std::move(half));
            }
            if (ride.track->circuit && !closed_before)
                scene.doors_requested = *b.ride;
            // Its end meeting where another open stretch starts, as across a gap left mid-track, joins that one on.
            auto& segments = ride.track->segments;
            for (std::size_t s = 0; !b.backward && segments.size() > 1 && !segments.back().closed && s + 1 < segments.size(); ++s)
            {
                const auto start = segments[s].closed || segments[s].piece_count == 0 ? std::nullopt : built_piece(scene, *c, ride.track->pieces.at(segments[s].first_piece));
                const auto at = start ? std::optional(park::start_of(scene.objects[start->object], c->pieces[start->laid.index], start->laid.reversed, scene.grid)) : std::nullopt;
                if (at && c->pieces[start->laid.index].record.side == shape.record.side && glm::length(b.ghost->next.position - at->position) < kCloses && b.ghost->next.end == at->end)
                {
                    park::join_stretch(*ride.track, s);
                    break;
                }
            }
            objects_changed(scene);
            track_changed(scene, b, assets);
            b.tip = track_tip(scene, b);
            refresh(scene, b, assets);
        }

        /// Shows `way` as the ghost, the pieces it lays.
        void show_way(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, std::span<const park::LaidPlacement> way)
        {
            std::vector<save::PlacedObject> shown;
            for (const park::LaidPlacement& p : way)
            {
                save::PlacedObject o = p.object;
                o.overlay = b.overlay;
                if (load_object_model(scene, b.models, assets, o))
                    shown.push_back(std::move(o));
            }
            b.ghost_mesh = park::object_mesh(shown, scene.models, scene.grid);
            for (park::GroundVertex& v : b.ghost_mesh.vertices)
                v.colour *= kGhostTint;
        }

        /// A click on auto-complete: starts looking for a way round to the station, stops looking while it looks,
        /// and lays the way found, paid for if the park has the money.
        void complete(ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            const park::PieceCatalogue* c = catalogue(b);
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            const auto last = last_piece(scene, b);
            if (b.completing)
            {
                b.completing.reset();
                return;
            }
            if (c == nullptr || !b.tip || !last || !ride.track || b.backward || b.selected)
                return;
            if (b.proposed.empty())
            {
                const save::TrackPiece& first = stretch_start(*ride.track);
                const auto objects = object_indices(scene);
                const auto object = objects.find(first.scenery);
                const auto shape = park::shape_named(*c, first.symbol);
                if (object == objects.end() || !shape)
                    return;
                const park::LaidPiece start { static_cast<std::size_t>(&*shape - c->pieces.data()), first.reversed };
                // shortcut: the way found may cross itself, as only what stands already is in its way; keep the pieces
                // laid in thought in its way too if a found way does.
                b.completing = park::start_completion(c->pieces, *b.tip, *last, park::start_of(scene.objects[object->second], *shape, first.reversed, scene.grid), start, scene.grid, [&scene, &b, &assets, taken = taken_room(scene, b)](save::PlacedObject o) {
                    o.overlay = b.overlay;
                    return !off_water(scene, b, assets, o) && !clashes(scene, taken, o);
                });
                return;
            }
            const std::vector<park::LaidPlacement> way = std::exchange(b.proposed, { });
            std::int64_t cost = 0;
            for (const park::LaidPlacement& p : way)
                cost += c->pieces[p.piece.index].record.cost;
            if (scene.park.cash && *scene.park.cash < cost)
            {
                b.complaint = "RideAutoCompleteNotEnoughCash";
                return;
            }
            logging::logger("park").info("track finished with {} pieces", way.size());
            for (const park::LaidPlacement& p : way)
            {
                const park::PieceShape& laid = c->pieces[p.piece.index];
                save::PlacedObject o = p.object;
                o.overlay = b.overlay;
                if (!load_object_model(scene, b.models, assets, o))
                    return;
                b.picked = p.piece;
                b.ghost = park::PiecePlacement { o, park::tip_of(o, laid, p.piece.reversed, scene.grid) };
                b.off_water = false; // The way was found on water
                b.clashes = false;   // and clear of what stands
                build(scene, b, assets);
            }
        }

        /// Takes the last piece laid out of the park and off the ride's track, which opens a closed circuit. A piece
        /// picked mid-track is taken out instead, leaving a gap that building goes on into from the piece before
        /// it, the same piece picked.
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
            save::TrackPiece last;
            const park::PieceCatalogue* c = catalogue(b);
            if (b.selected)
            {
                last = park::take_out_piece(*ride.track, *std::exchange(b.selected, std::nullopt));
                if (const auto laid = c != nullptr ? laid_piece(*c, last) : std::nullopt)
                    b.choice = park::choice_of(c->pieces, *laid);
                use_split(scene, b, b.split);
            }
            else
            {
                // Built backwards, the first piece is the one laid last.
                const bool front = b.backward && ride.track->pieces.size() > 1;
                last = front ? ride.track->pieces.front() : ride.track->pieces.back();
                ride.track->pieces.erase(front ? ride.track->pieces.begin() : ride.track->pieces.end() - 1);
                ride.track->circuit = false;
                if (!ride.track->segments.empty() && ride.track->segments.back().piece_count > 0)
                    --ride.track->segments.back().piece_count;
                // A stretch emptied gives way to the one before, open at its end again.
                if (ride.track->segments.size() > 1 && ride.track->segments.back().piece_count == 0)
                    ride.track->segments.pop_back();
                if (!ride.track->segments.empty())
                    ride.track->segments.back().closed = false;
            }
            const auto objects = object_indices(scene);
            if (const auto found = objects.find(last.scenery); found != objects.end())
                scene.objects[found->second].hidden = true;
            hide_platform(scene, last);
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
        wait_for_build(scene); // The build beside the frame reads the models
        scene.models.emplace(key, std::move(*model));
        for (const auto& [name, picture] : m.loader->pictures())
            scene.pictures.try_emplace(name, picture);
        return true;
    }

    void objects_changed(ParkScene& scene)
    {
        const profiling::Scope zone("ObjectsChanged");
        scene.supports.stale = true;
        wait_for_build(scene); // The build beside the frame reads the anchors written here
        scene.anchors = park::object_anchors(scene.objects, scene.models, scene.grid, scene.fitted);
        scene.lamps = park::object_lamps(scene.objects, scene.models, scene.grid, scene.fitted);
        scene.lamps_due = true;
        show_park_particles(scene.particles, park::object_effects(scene.objects, scene.models, scene.grid, scene.fitted));
        scene.levels = { };
        scene.since_rebuild = kObjectRebuildInterval;
        scene.pick_boxes_of = 0; // A piece built over keeps the count the boxes are kept by
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
        fit_texture_detail(scene, pictures);
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
        scene.building.selected.reset();
        scene.building.ghost.reset();
        scene.building.ghost_mesh = { };
        scene.building.tabs_for.reset();
        scene.building.completing.reset();
        scene.building.proposed.clear();
        scene.building.test.reset();
        if (auto panel = scene.desktop->root().find(kPanelName))
            panel->close();
        if (auto placing = scene.desktop->root().find_as<ui::InfoPanel>(kPlacingName))
        {
            placing->set_body_size(0, 0);
            placing->set_paper(false);
        }
        if (auto arm = scene.desktop->root().find(kArmName))
            arm->close();
        if (auto browser = scene.desktop->root().find(kBrowserName))
            browser->close();
        scene.building.offer_designs = false;
        scene.building.design_shown.reset();
        if (auto placing = scene.desktop->root().find_as<ui::InfoPanel>(kPlacingName); placing && placing->showing() && !ride_placing(scene))
        {
            placing->hide();
            placing->show_view_text({ });
            scene.placing.preview_mesh = { };
        }
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

    std::optional<bool> use_train(ParkScene& scene, const config::GameInstall& install, std::size_t index, std::string_view train_name)
    {
        save::Ride& ride = scene.rides.rides[index];
        if (const auto file = train_file(install, train_name))
        {
            std::string path = std::filesystem::relative(*file, install.root()).generic_string();
            path.resize(path.size() - std::string_view(".common.ovl").size());
            ride.trains.assign(1, save::Train { });
            save::Train& train = ride.trains.front();
            train.symbol = std::format("{}:rit", train_name);
            train.overlay_path = path;
            // As many cars as its kind usually has, and the fewest and most it may have.
            ui::TrainLimits& limits = scene.train_limits[index];
            limits = { };
            const auto car_file = ovl::Overlay::load(*file);
            for (const auto part : car_file ? std::array { maybe(&car_file->common()), car_file->unique() } : std::array<std::optional<const ovl::Part&>, 2> { })
                for (const ovl::RideTrainItem& t : part ? ovl::read_ride_trains(*car_file, *part) : std::vector<ovl::RideTrainItem> { })
                    if (t.train && strings::equals_ignoring_case(t.name, train_name) && t.train->fewest_cars > 0 && t.train->fewest_cars <= t.train->most_cars)
                        limits = { .fewest_cars = static_cast<int>(t.train->fewest_cars), .most_cars = static_cast<int>(t.train->most_cars), .usual_cars = static_cast<int>(std::clamp(t.train->usual_cars, t.train->fewest_cars, t.train->most_cars)) };
            return fit_trains(scene, install, index, 1, limits.usual_cars);
        }
        return std::nullopt;
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
        const auto colours = park::name_colours(item.symbol);
        ride.car_colours = { static_cast<std::uint8_t>(colours[0]), static_cast<std::uint8_t>(colours[1]), static_cast<std::uint8_t>(colours[2]) };
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
                    scene.train_choices[index].assign(t.ride->operating->trains.begin(), t.ride->operating->trains.end());
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
        // Its riders sit on arms.
        const bool arm = !train_name.empty() && use_train(scene, *assets.install, index, train_name).value_or(false);
        if (!ride.trains.empty())
            logging::logger("park").info("new {} runs {} with {} seats, lift {} m/s", item.symbol, train_name, ride.seats.size(), ride.options.lift_speed);
        // The simulation takes it on once its entrance and exit are placed beside its station (ride_placing.hpp).
        scene.track_rules.resize(scene.rides.rides.size());
        scene.track_rules[index] = park::load_track_rules(*file, ride.base.symbol);
        scene.courses.resize(scene.rides.rides.size());
        logging::logger("park").info("new {} built from {}", item.symbol, *pieces);
        const bool opened = open_track_building(scene, index, *pieces, assets);
        scene.building.arm = scene.building.arm || arm;
        // The saved designs of its kind, offered in their window when it was picked from a list.
        scene.building.designs = designs_for(scene.build, item.symbol);
        scene.building.offer_designs = std::exchange(scene.build.offer_designs, false) && opened && !scene.building.designs.empty();
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

    bool track_building_click(ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size)
    {
        TrackBuilding& b = scene.building;
        if (!b.ride)
            return false;
        b.completing.reset();
        b.proposed.clear();
        save::Ride& ride = scene.rides.rides.at(*b.ride);
        const park::PieceCatalogue* c = catalogue(b);
        if (c == nullptr || !ride.track)
            return false;
        if (!opening(ride))
        {
            if (size.x <= 0 || size.y <= 0)
                return false;
            const park::Ray ray = park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
            if (scene.pick_boxes_of != scene.objects.size())
            {
                scene.pick_boxes = park::pick_boxes(scene.objects, scene.models, scene.grid, scene.fitted);
                scene.pick_boxes_of = scene.objects.size();
            }
            // Only its pieces, not its doors or platforms.
            std::unordered_set<std::uint64_t> pieces;
            for (const save::TrackPiece& p : ride.track->pieces)
                pieces.insert(p.scenery);
            const auto hit = park::pick_object(ray, scene.pick_boxes, scene.grid, [&](std::size_t i) { return !scene.objects[i].hidden && pieces.contains(scene.objects[i].id); });
            const auto piece = hit ? std::ranges::find(ride.track->pieces, scene.objects[hit->index].id, &save::TrackPiece::scenery) : ride.track->pieces.end();
            if (piece == ride.track->pieces.end())
                return false;
            // A piece at an open end goes back to building at the track's open end.
            // shortcut: only the last stretch's far end is built on, upgrade by building on from the end clicked.
            const std::size_t index = static_cast<std::size_t>(piece - ride.track->pieces.begin());
            const auto laid = laid_piece(*c, *piece);
            b.selected = laid && park::piece_neighbours(*ride.track, index) ? std::optional(index) : std::nullopt;
            b.complaint.clear();
            if (!b.selected)
                use_split(scene, b, b.split);
            else if (b.split != park::SplitState::None)
                b.split = park::split_of(c->pieces[laid->index].record);
            if (b.selected)
            {
                // Building steps on toward the end of the piece nearer the click.
                const save::PlacedObject& o = scene.objects[hit->index];
                const park::PieceShape& shape = c->pieces[laid->index];
                const glm::vec3 point = ray.origin + ray.direction * hit->distance;
                b.selected_back = glm::length(point - park::start_of(o, shape, laid->reversed, scene.grid).position) < glm::length(point - park::tip_of(o, shape, laid->reversed, scene.grid).position);
                b.choice = park::choice_of(c->pieces, *laid);
            }
            refresh(scene, b, assets);
            return true;
        }
        if (!b.ghost)
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
        b.selected.reset();
        b.aim.reset();
        b.turns = 0;
        b.tip = track_tip(scene, b);
        b.arm = std::ranges::any_of(scene.arm_cars, [&](const ParkScene::ArmCar& a) { return scene.car_spots[a.car].ride == ride; });
        b.arm_piece = 0;
        b.arm_back = 0;
        b.designs.clear();
        b.design.clear();
        b.design_ride.reset();
        b.tabs_for.reset();
        b.colours.reset();
        b.preview_item.clear();
        // A ride of two tracks side by side starts with joined pieces, from the left track's end.
        const auto& pieces = b.catalogues.at(b.overlay).pieces;
        use_split(scene, b, std::ranges::any_of(pieces, [](const park::PieceShape& p) { return p.record.side != 0; }) ? park::SplitState::JoinedLeft : park::SplitState::None);
        refresh(scene, b, assets);
        auto placing = scene.desktop->root().find_as<ui::InfoPanel>(kPlacingName);
        if (!placing)
            return false;
        if (!scene.desktop->root().find(kPanelName))
        {
            auto& panel = placing->body().add<ui::ConstructionPanel>("construction", ui::game_skins::construction_panel_style(assets.palette), std::array { std::u16string(assets.strings->get(kToggleHelps[0])), std::u16string(assets.strings->get(kToggleHelps[1])), std::u16string(assets.strings->get(kToggleHelps[2])), std::u16string(assets.strings->get(kToggleHelps[3])), std::u16string(assets.strings->get(kToggleHelps[4])) }, std::u16string(assets.strings->get(kDemolishHelp)));
            panel.placement = ui::top_left(-kControlsLeft, -kControlsTop, ui::construction_panel::kWidth, ui::construction_panel::kHeight);
            panel.complete_button().tooltip = assets.strings->get(kCompleteHelp);
            panel.new_segment_button().tooltip = assets.strings->get(kNewSegmentHelp);
            panel.split_button().tooltip = assets.strings->get(kSplitHelp);
            panel.heights_help = assets.strings->get("ATE_TT_HeightMarks");
            panel.facing_button().tooltip = assets.strings->get("ATE_TT_SpinAngle");
            using enum ui::PieceParam;
            for (const auto& [param, help] : { std::pair { CarTurn, "TrackDesigner_CarSpin_TT" }, std::pair { HoldingTime, "TrackDesigner_HoldingTime_TT" }, std::pair { Speed, "TrackDesigner_BrakeSpeed_TT" } })
                panel.param_spinner(param).tooltip = assets.strings->get(help);
            panel.heights_locked_help = assets.strings->get("ATE_TT_HeightMarksLocked");
        }
        // A ride that is one building is set down whole, as a flat ride is: the round panel shows no strip,
        // and the controls keep the building's state out of sight.
        const bool whole = ride < scene.track_rules.size() && scene.track_rules[ride].one_piece;
        scene.desktop->root().find(kPanelName)->visible = !whole;
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
                strip->placement = ui::bottom_left(ui::construction_panel::kStripLeft, kPanelTop, 540, kRow + 8);
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

    void update_track_building(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, int currency, ui::DistanceUnits units, std::optional<save::Tile> pointed, std::span<const ui::PlayCommand> commands, float elapsed)
    {
        TrackBuilding& b = scene.building;
        b.clock += elapsed;
        auto browser = scene.desktop->root().find_as<ui::DesignBrowser>(kBrowserName);
        if (b.design_picture && !browser)
        {
            // The design's picture goes with its window.
            device.destroy_texture(*b.design_picture);
            b.design_picture.reset();
        }
        auto panel = scene.desktop->root().find_as<ui::ConstructionPanel>(kPanelName);
        auto placing = scene.desktop->root().find_as<ui::InfoPanel>(kPlacingName);
        if (!panel || !placing || !b.ride)
            return;
        if (placing->take_closed())
        {
            close_track_building(scene);
            return;
        }
        const park::PieceCatalogue* c = catalogue(b);
        bool changed = false;
        // The round panel's ring, in this order: the ride's window, turning the first piece, the construction
        // strip, its state, and the colours of the pieces built next. The turn shows greyed while it does nothing.
        enum Tab : std::size_t
        {
            Info,
            Rotate,
            Construction,
            State,
            Colour,
        };
        // A ride set down whole has no strip to show.
        const bool one_piece = *b.ride < scene.track_rules.size() && scene.track_rules[*b.ride].one_piece;
        const bool starting = opening(scene.rides.rides.at(*b.ride));
        if (const auto tab = placing->take_action())
        {
            if (*tab == Info)
                scene.info_requested = park::AttractionRef { park::AttractionRef::Kind::Ride, *b.ride };
            // Each press turns the first piece a quarter, as a flat ride's turn does.
            if (*tab == Rotate && starting)
            {
                b.turns = (b.turns + 1) % 4;
                changed = true;
            }
            // Each press shows or hides the strip of construction controls.
            if (*tab == Construction && !one_piece)
                panel->visible = !panel->visible;
        }
        const bool whole = !panel->visible;
        if (const auto paint = placing->take_paint(); paint && paint->pot < std::tuple_size_v<save::RideColours>)
        {
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            save::RideColours colours = next_colours(ride, b);
            colours.at(paint->pot) = static_cast<std::uint8_t>(paint->colour);
            b.colours = colours;
            b.tabs_for.reset();
        }
        const park::AttractionRef ref { .kind = park::AttractionRef::Kind::Ride, .index = *b.ride };
        const std::vector<ui::StateChoice> states = state_choices(scene, ref);
        if (const auto choice = placing->take_choice(); choice && choice->first == State && choice->second < states.size())
        {
            scene.refusal = change_ride_state(scene, *b.ride, states[choice->second].state).value_or("");
            b.tabs_for.reset();
        }
        if (const std::tuple now { starting, scene.rides.rides.at(*b.ride).base.state, whole }; b.tabs_for != now || !placing->showing())
        {
            std::vector<ui::InfoTab> tabs(5);
            for (const ui::AttractionTabInfo& t : ui::attraction_tabs(ui::AttractionKind::Tracked, std::get<1>(now)))
            {
                if (t.tab != ui::AttractionTab::View && t.tab != ui::AttractionTab::State && t.tab != ui::AttractionTab::Construction)
                    continue;
                ui::InfoTab& tab = tabs[t.tab == ui::AttractionTab::View ? Info : t.tab == ui::AttractionTab::State ? State : Construction];
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
            }
            tabs[Rotate].icon = starting ? "IconAttractionEditorRotate" : "IconAttractionEditorRotate_Disabled";
            tabs[Rotate].help = assets.strings->get("ATE_TT_Rotate");
            if (one_piece)
                tabs[Construction].icon = "Attraction_Construction_Disabled";
            const save::Ride& ride = scene.rides.rides.at(*b.ride);
            const save::RideColours colours = next_colours(ride, b);
            tabs[Colour].icon = "Attraction_Colour";
            tabs[Colour].help = assets.strings->get("Scenery_SetFlexiColours");
            tabs[Colour].paints = { colours.begin(), colours.end() };
            tabs[State].choices = state_fan(scene, ref, *assets.strings);
            for (std::size_t i = 0; i < tabs.size(); ++i)
            {
                tabs[i].slot = static_cast<int>(i);
                tabs[i].action = tabs[i].choices.empty() && tabs[i].paints.empty();
            }
            const std::size_t none = tabs.size(); // No tab open: the strip holds the controls
            placing->show(std::move(tabs), none);
            placing->set_body_size(whole ? 0 : kControlsWidth, whole ? 0 : kControlsHeight);
            placing->set_paper(!whole);
            b.tabs_for = now;
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
            if (event->action != Complete)
            {
                b.completing.reset();
                b.proposed.clear();
            }
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
                case Toggle:
                    b.choice = park::pick_toggle(c->pieces, b.candidates, b.choice, static_cast<park::PieceToggle>(event->index));
                    break;
                case Named:
                {
                    const std::vector<ListedPiece> list = listed_pieces(*assets.strings, *c, park::choice_options(c->pieces, b.candidates, b.choice));
                    if (event->index < list.size() && list[event->index].laid)
                        b.choice.named = list[event->index].laid;
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
                case Heights:
                    b.heights = !b.heights;
                    break;
                case Param:
                    switch (static_cast<ui::PieceParam>(event->index))
                    {
                        case ui::PieceParam::CarTurn:
                            b.car_turn = event->value;
                            break;
                        case ui::PieceParam::HoldingTime:
                            b.holding_time = event->value;
                            break;
                        case ui::PieceParam::Speed:
                            scene.rides.rides.at(*b.ride).options.brake_speed = static_cast<float>(event->value) / 100 / ui::speed_scale(units);
                            break;
                    }
                    break;
                case Facing:
                    b.facing = static_cast<std::uint8_t>((b.facing + 1) % ui::construction_panel::kFacings);
                    break;
                case NewSegment:
                {
                    // shortcut: cars run the stretches as one course, jumping between them, upgrade when the course
                    // follows each stretch on its own.
                    save::Track& track = *scene.rides.rides.at(*b.ride).track;
                    track.segments.push_back({ .first_piece = track.pieces.size() });
                    b.selected.reset();
                    b.design.clear();
                    b.design_ride.reset();
                    break;
                }
                case Split:
                    // Over a piece picked mid-track, only the kind of pieces changes.
                    if (b.selected)
                        b.split = park::next_split(b.split, split_closed(scene, b), true);
                    else
                        use_split(scene, b, park::next_split(b.split, split_closed(scene, b), false));
                    break;
            }
            changed = true;
        }
        if (b.completing)
        {
            if (const auto found = park::search_on(*b.completing, kCompletionTriesPerFrame))
            {
                if (*found)
                    b.proposed = std::move(**found);
                else
                    b.complaint = "RideAutoCompleteNoSolution";
                b.completing.reset();
            }
            else if (b.completing->tried >= kCompletionTries)
            {
                b.complaint = "RideAutoCompleteOutOfTime";
                b.completing.reset();
            }
            changed = true;
        }
        if (changed)
            refresh(scene, b, assets);
        // A test of a track that is not yet a circuit: the park runs no train on it, so a ghost of its first train
        // runs it, stopping where it ends. A circuit's own trains test it.
        const save::Ride& tested = scene.rides.rides.at(*b.ride);
        const auto& course = *b.ride < scene.courses.size() ? scene.courses[*b.ride] : std::nullopt;
        const bool testable = tested.base.state == save::AttractionState::Testing && course && !course->closed && std::ranges::contains(course->kinds, park::CourseKind::Station) && !tested.trains.empty() && scene.car_fits.contains(*b.ride) && assets.install;
        if (!testable)
            b.test.reset();
        else if (!b.test)
        {
            // The ride's first train, stood with its front where trains set off. The course is taken as a circuit
            // to make the train, which is then told the track has ends.
            save::Ride ghost = tested;
            ghost.trains.resize(1);
            park::TrackCourse round = *course;
            round.closed = true;
            if (auto train = simulation::coaster_of(round, ghost))
            {
                train->closed = false;
                train->ghost = true;
                train->running = true;
                park::stand_train(*course, ghost.trains.front(), scene.car_fits.at(*b.ride), train->start);
                const park::RideCars cars = park::ride_cars(std::span(&ghost, 1), 0);
                load_car_models(scene, *assets.install, cars);
                TrackBuilding::GhostTrain& t = b.test.emplace(TrackBuilding::GhostTrain { .train = *train, .spots = cars.spots, .stood_at = ghost.trains.front().distance });
                for (const save::PlacedObject& car : cars.pieces)
                    t.stood.push_back(cars.fitted.at(car.id));
                t.meshes = park::moving_meshes(cars.pieces, scene.models, scene.grid);
                for (std::size_t i = 0; i < cars.pieces.size(); ++i)
                    if (const auto model = scene.models.find(park::model_ref(cars.pieces[i])); model != scene.models.end())
                        t.joints.insert(t.joints.end(), model->second.arm_joints.size(), i);
                for (auto* vertices : { &t.meshes.mesh.vertices, &t.meshes.own })
                    for (park::GroundVertex& v : *vertices)
                        v.colour *= kGhostTint;
                ghost_textures(device, scene, t.meshes.mesh);
            }
        }
        if (b.test)
        {
            TrackBuilding::GhostTrain& t = *b.test;
            simulation::drive(t.train, elapsed);
            // Stood at the open end a while, it is back at the station and sets off again.
            if (!t.train.running && (t.waited += elapsed) >= kTestWait)
            {
                t.train.at = t.train.start;
                t.train.travelled = 0;
                t.train.launched = false;
                t.train.running = true;
                t.waited = 0;
            }
            std::vector<glm::mat4> places;
            for (std::size_t i = 0; i < t.stood.size(); ++i)
                places.push_back(park::moved_along(*course, t.stood[i], { t.spots[i].front, t.spots[i].rear }, t.train.at - t.stood_at));
            // shortcut: the joints of cars on arms ride at rest with their car, upgrade if a test should swing them
            // as the park does.
            for (const std::size_t car : t.joints)
            {
                const glm::mat4 place = places[car];
                places.push_back(place);
            }
            park::move_meshes(t.meshes, places);
        }
        if (b.completing)
            show_way(scene, b, assets, park::nearest_way(*b.completing));
        else if (!b.proposed.empty())
            show_way(scene, b, assets, b.proposed);
        ghost_textures(device, scene, b.ghost_mesh);
        panel->show(panel_view(scene, b, assets, currency, units));
        // The disc shows the piece to lay alone, turning, with its price across it.
        std::u16string price;
        if (b.ghost && b.picked && c != nullptr)
        {
            save::PlacedObject alone = b.ghost->object;
            alone.col = alone.row = 0;
            alone.base_height = 0;
            alone.force_absolute_height = true;
            if (b.preview_item != alone.symbol)
            {
                const std::array<save::PlacedObject, 1> one { alone };
                scene.placing.preview_mesh = park::object_mesh(one, scene.models, scene.grid);
                ghost_textures(device, scene, scene.placing.preview_mesh);
                scene.placing.preview_bones.clear();
                b.preview_item = alone.symbol;
            }
            scene.placing.preview_camera = park::framed(park::object_corners(alone, scene.models, scene.grid, scene.fitted), kPreviewTurn * scene.clock, kPreviewPitch, kPreviewClosest);
            price = ui::money_text(ui::currency(*assets.strings, currency), c->pieces[b.picked->index].record.cost);
        }
        else
        {
            scene.placing.preview_mesh = { };
            b.preview_item.clear();
        }
        placing->show_view_text(std::move(price));
        update_arm_strip(scene, b, assets);
        // The saved designs' window, opened for a new ride picked from a list: custom track first, then each
        // design by its translated name or its file's, sorted by the heading pressed.
        if (std::exchange(b.offer_designs, false) && !browser)
        {
            const ui::StringTable& strings = *assets.strings;
            // shortcut: a design opens once any career of the base game has completed its scenario, not only the
            // one being played; upgrade once the park knows its career.
            std::vector<career::Career> careers;
            if (scene.saves.data)
                for (career::CareerEntry& e : career::list_careers(scene.saves.data->folder(config::UserFolder::Careers), 0, std::nullopt))
                    careers.push_back(std::move(e.data.career));
            const auto heading = [&](std::string_view key) {
                std::u16string h(strings.get(key));
                while (!h.empty() && (h.back() == u':' || h.back() == u' '))
                    h.pop_back();
                return h;
            };
            std::vector<ui::TableColumn> columns { { .heading = heading("Staff_ColumnHeader_Name") }, { .heading = heading("AttractionResultsDialogLabel_Excitement"), .width = 120, .align = ui::Align::End }, { .heading = heading("AttractionResultsDialogLabel_Intensity"), .width = 120, .align = ui::Align::End } };
            std::vector<ui::TableRow> rows { { .cells = { std::u16string(strings.get(kCustomTrack)), { }, { } }, .keys = { }, .id = 0 } };
            b.designs_locked.clear();
            for (std::size_t i = 0; i < b.designs.size(); ++i)
            {
                const save::DesignSummary& d = b.designs[i].summary;
                const std::string name = design_name(b.designs[i].path);
                std::string key(kDesignText);
                std::ranges::copy_if(name, std::back_inserter(key), [](char c) { return c != ' ' && c != '\'' && c != '-'; });
                std::u16string text(strings.get(key));
                if (text.empty())
                    text = strings::to_utf16(name);
                const bool locked = career::design_locked(d.scenario, d.level, careers);
                b.designs_locked.push_back(locked);
                if (locked)
                    text += strings.get(kLockedSuffix);
                const auto rated = [&](float v) { return d.assessment.valid ? strings::to_utf16(std::format("{:.2f}", v)) : std::u16string { }; };
                const auto key_of = [&](float v) { return d.assessment.valid ? std::optional<double>(v) : std::nullopt; };
                rows.push_back({ .cells = { std::move(text), rated(d.assessment.excitement), rated(d.assessment.intensity) }, .keys = { std::nullopt, key_of(d.assessment.excitement), key_of(d.assessment.intensity) }, .id = i + 1 });
            }
            browser = scene.desktop->root().add<ui::DesignBrowser>(std::string(kBrowserName), ui::game_skins::design_browser_style(assets.palette), std::u16string(strings.get("TrackBrowser_Title")), "Ride_Coaster", std::move(columns), std::move(rows), std::u16string(strings.get("TrackBrowser_BuildThis")), std::u16string(strings.get("TrackBrowser_Cancel")));
            b.browsing = true;
        }
        if (!browser)
        {
            // Closed from its frame: nothing is built, as with Cancel.
            if (std::exchange(b.browsing, false))
                close_track_building(scene);
            return;
        }
        const std::vector<std::string>& trains = scene.train_choices[*b.ride];
        const auto show_train = [&] {
            std::u16string name;
            if (b.design_train < trains.size())
            {
                name = strings::to_utf16(trains[b.design_train]);
                // Named as its file names it, else by its own name.
                if (const auto file = train_file(*assets.install, trains[b.design_train]))
                    if (const auto car_file = ovl::Overlay::load(*file))
                        for (const auto part : std::array { maybe(&car_file->common()), car_file->unique() })
                            for (const ovl::RideTrainItem& t : part ? ovl::read_ride_trains(*car_file, *part) : std::vector<ovl::RideTrainItem> { })
                                if (const auto text = t.train ? assets.strings->get(t.train->display_name) : std::u16string_view { }; !text.empty())
                                    name = text;
            }
            browser->show_train(std::move(name), trains.size() > 1);
        };
        if (const auto picked = browser->take_picked())
        {
            if (b.design_picture)
                device.destroy_texture(*b.design_picture);
            b.design_picture.reset();
            b.design_shown = *picked == 0 ? std::nullopt : std::optional(*picked - 1);
            ui::TextureId picture = 0;
            std::vector<ui::InfoLine> lines;
            bool locked = false;
            if (b.design_shown)
            {
                // Read again for its picture, which the list of designs does not keep.
                std::ifstream in(b.designs[*b.design_shown].path, std::ios::binary);
                const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                const auto d = save::read_design_summary(std::as_bytes(std::span(raw)));
                const save::DesignSummary& summary = d ? *d : b.designs[*b.design_shown].summary;
                if (!summary.picture.empty())
                    if (auto texture = device.create_texture({ summary.picture_width, summary.picture_height, render::TextureFormat::Rgba8, std::as_bytes(std::span(summary.picture)), "design picture" }))
                    {
                        b.design_picture = *texture;
                        picture = ui::texture_id(*texture);
                    }
                lines = ui::design_lines(summary, *assets.strings, { ui::currency(*assets.strings, currency), units });
                locked = b.designs_locked.at(*b.design_shown);
                if (locked)
                    lines.push_back({ .label = std::u16string(assets.strings->get(kLockedHelp)), .name = std::string(kLockedHelp) });
                b.design_train = trains.empty() ? 0 : static_cast<std::size_t>(std::clamp<std::int32_t>(summary.train, 0, static_cast<std::int32_t>(trains.size()) - 1));
            }
            browser->show_design(picture, std::move(lines), !locked);
            if (b.design_shown)
                show_train();
            else
                browser->show_train({ }, false);
        }
        if (const auto step = browser->take_train_step(); step && b.design_shown && !trains.empty())
        {
            b.design_train = (*step > 0 ? b.design_train + 1 : b.design_train + trains.size() - 1) % trains.size();
            show_train();
        }
        if (browser->take_cancel())
        {
            close_track_building(scene);
            return;
        }
        if (const auto build = browser->take_build())
        {
            b.browsing = false;
            browser->close();
            // A design is laid whole by the next click, with the train picked for it; a custom track is built
            // piece by piece.
            const auto design = *build > 0 ? design_ride(b.designs.at(*build - 1).path) : std::nullopt;
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
            if (design && b.design_train < trains.size())
                if (const auto arm = use_train(scene, *assets.install, *b.ride, trains[b.design_train]))
                    b.arm = *arm || b.arm;
            refresh(scene, b, assets);
        }
    }

    void draw_track_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.building.ride)
            draw_ghost_mesh(device, scene, scene.building.ghost_mesh, transform, light);
        if (scene.building.test)
            draw_ghost_mesh(device, scene, scene.building.test->meshes.mesh, transform, light);
    }

    render::SceneLight ghost_light(render::SceneLight light, float clock)
    {
        // The strength swings between these, rising and falling evenly, this many times a second.
        constexpr float kDimmest = 0.7f;
        constexpr float kBrightest = 1.1f;
        constexpr float kPulses = 0.6f;
        const float wave = 2 * std::abs(clock * kPulses - std::floor(clock * kPulses) - 0.5f); // 1 to 0 and back
        light.sky = glm::vec3(std::lerp(kDimmest, kBrightest, wave));
        light.ghost = true;
        light.haze_rate = 0;
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
            draw.light = solid ? light : ghost_light(light, scene.clock);
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
