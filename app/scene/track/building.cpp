// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/track/building.hpp"

#include "career/career_file.hpp"
#include "logging/log.hpp"
#include "ovl/tracked_ride.hpp"
#include "park/build_arrows.hpp"
#include "park/track/edit.hpp"
#include "scene/ghosts.hpp"
#include "scene/park/attraction_state.hpp"
#include "scene/park/footpaths.hpp"
#include "scene/park/particles.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track/laying.hpp"
#include "scene/track/trains.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/rides.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/career_scenarios.hpp"
#include "ui/game_style.hpp"
#include "ui/interface_assets.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/build/designs.hpp"
#include "ui/screens/build/track.hpp"
#include "ui/screens/info/attraction.hpp"
#include "ui/screens/info/panel.hpp"
#include "ui/widgets/panel.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/tickbox.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace rct3::scene::track
{

    namespace
    {
        /// The building controls, in the strip of the round panel in the bottom-left corner.
        constexpr std::string_view kPanelName = "placing/body/construction";
        constexpr std::string_view kArmName = "construction_arm";   ///< The strip of arm angles above the panel
        constexpr std::string_view kBrowserName = "design_browser"; ///< The saved designs' window
        constexpr std::string_view kCustomTrack = "TrackBrowser_CustomTrack";
        constexpr std::string_view kLockedSuffix = "TrackBrowser_LockedSuffix";
        constexpr std::string_view kLockedHelp = "TrackBrowser_LockedHelp"; ///< The name of its line
        constexpr std::string_view kDesignText = "TrackDesign_";            ///< Before a design's name, for its translated name
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
        /// In the order of rct3::park::track::PieceToggle.
        constexpr std::array<std::string_view, rct3::park::track::kPieceToggles> kToggleHelps { "ATE_TT_Chain", "ATE_TT_Enclosed", "ATE_TT_Water", "ATE_TT_Wide", "ATE_TT_Booster" };
        constexpr std::string_view kDemolishHelp = "ATE_TT_Demolish";
        constexpr std::string_view kCannotPlace = "IllegalTrackPlacement";
        constexpr std::string_view kNotOwned = "PlaceObject_NotOwned"; ///< A piece off land the park owns
        constexpr std::string_view kInGround = "PlaceObject_IntersectsGround";
        constexpr std::string_view kCostLabel = "ShopItem_CostValue";
        /// The roles track files give pieces (ovl::TrackSection::role) that have a picture of their own in the
        /// construction panel, and the picture.
        struct RoleIcon
        {
            std::uint32_t role = 0;
            std::string_view skin;
        };
        constexpr std::uint32_t kBrakesRole = 1;
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
        constexpr float kFlashSeconds = 0.4f;              ///< The found way's button is lit, then dim, this long
        constexpr float kTestWait = 3;                     ///< Seconds a test train stands stuck before it sets off again
        constexpr std::string_view kPieceNames = "Track_"; ///< Followed by the section's name
        /// Above the tool row, so the two never overlap.
        /// Of the round panel's top, from the screen's bottom edge: its disc stands as the path panel does.
        constexpr float kPanelTop = ui::game_skins::kOverToolRow + ui::screens::info::info_panel::kDisc;
        /// The controls' room in the strip: from a little left of their rows to a little right of their pager,
        /// as high as the strip holds.
        constexpr float kControlsLeft = ui::screens::build::construction_panel::kRowLeft - 8;
        constexpr float kControlsTop = 4;
        constexpr float kControlsWidth = ui::screens::build::construction_panel::kListRight + 4 - kControlsLeft;
        constexpr float kControlsHeight = ui::screens::build::construction_panel::kHeight - kControlsTop;
        /// The ghost's tint and how much of what is behind it shows through.

        using rct3::park::track::kBankSteps;

        /// The panel's bank buttons run from hardest left to hardest right, upside down last; banks count left
        /// as positive.
        int panel_bank(std::size_t i)
        {
            return i + 1 == ui::screens::build::construction_panel::kBanks ? rct3::park::track::kUpsideDown : kBankSteps - 1 - static_cast<int>(i);
        }

        /// A saved design: its ride, and the scenery placed with it, which is the ride's doors.
        struct SavedDesign
        {
            save::model::Ride ride;
            std::vector<save::model::PlacedObject> scenery;
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
            auto rides = save::model::load_rides(*saved, index);
            if (!rides || rides->rides.empty() || !rides->rides.front().track)
                return std::nullopt;
            auto world = save::model::load_world(*saved, index);
            return SavedDesign { std::move(rides->rides.front()), world ? std::move(world->objects) : std::vector<save::model::PlacedObject> { } };
        }

        /// The name of a design's file without the kind in brackets before it ("Giga" of "[Giga]Giga.trk").
        std::string design_name(const std::filesystem::path& file)
        {
            const std::string stem = file.stem().string();
            const std::size_t close = stem.find(']');
            return stem.substr(close == std::string::npos ? 0 : close + 1);
        }

        /// A design's name as the player reads it: translated where the game names it, else its file's.
        std::u16string design_title(const ui::StringTable& strings, const std::filesystem::path& file)
        {
            const std::string name = design_name(file);
            std::string key(kDesignText);
            std::ranges::copy_if(name, std::back_inserter(key), [](char c) { return c != ' ' && c != '\'' && c != '-'; });
            const std::u16string text(strings.get(key));
            return text.empty() ? strings::to_utf16(name) : text;
        }

        /// Makes the park's own trains of `ride` ghosts, whose crash off an open end of the track harms nothing and
        /// is not the park's, or real trains again.
        void ghost_trains(park::ParkScene& scene, std::size_t ride, bool ghost)
        {
            const entt::entity e = ride < scene.ride_entities.size() ? scene.ride_entities[ride] : entt::null;
            if (auto* fleet = e != entt::null ? scene.sim.registry.try_get<simulation::Trains>(e) : nullptr)
                for (simulation::Coaster& c : fleet->trains)
                    c.ghost = ghost;
        }

        ui::screens::build::IconState state_of(bool picked, bool offered)
        {
            return picked ? ui::screens::build::IconState::On : offered ? ui::screens::build::IconState::Off : ui::screens::build::IconState::Dis;
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
            ui::screens::build::NamedPiece shown;
            std::optional<rct3::park::track::LaidPiece> laid;
        };

        /// Every piece of `c` with a use of its own that `rules` let the ride build, stations first, then in the
        /// file's order, once for each name and marks; those `o` offers where the track ends are offered.
        std::vector<ListedPiece> listed_pieces(const ui::StringTable& strings, const rct3::park::track::PieceCatalogue& c, const rct3::park::track::TrackRules& rules, const rct3::park::track::ChoiceOptions& o)
        {
            const auto shown_as = [&](const rct3::park::track::PieceShape& p) {
                ui::screens::build::NamedPiece n { .name = piece_name(strings, p.record.name), .offered = false };
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
            const auto same = [](const ui::screens::build::NamedPiece& a, const ui::screens::build::NamedPiece& b) { return a.name == b.name && a.marks == b.marks; };
            std::vector<ListedPiece> list;
            for (const rct3::park::track::PieceUse use : { rct3::park::track::PieceUse::Station, rct3::park::track::PieceUse::Special })
            {
                for (const rct3::park::track::PieceShape& p : c.pieces)
                {
                    if (p.use != use || !rct3::park::track::lists_piece(rules, p.record.name))
                        continue;
                    ui::screens::build::NamedPiece n = shown_as(p);
                    if (std::ranges::none_of(list, [&](const ListedPiece& l) { return same(l.shown, n); }))
                        list.push_back({ std::move(n), std::nullopt });
                }
            }
            for (const rct3::park::track::LaidPiece& laid : o.named)
            {
                const ui::screens::build::NamedPiece n = shown_as(c.pieces[laid.index]);
                const auto found = std::ranges::find_if(list, [&](const ListedPiece& l) { return same(l.shown, n) && !l.laid; });
                if (found == list.end())
                    continue;
                found->laid = laid;
                found->shown.offered = true;
            }
            return list;
        }

        /// The panel's view of what the building offers now.
        ui::screens::build::ConstructionView panel_view(const park::ParkScene& scene, const TrackBuilding& b, const ui::InterfaceAssets& assets, int currency, ui::screens::info::DistanceUnits units)
        {
            ui::screens::build::ConstructionView v;
            v.curves.fill(ui::screens::build::IconState::Dis);
            v.slopes.fill(ui::screens::build::IconState::Dis);
            v.banks.fill(ui::screens::build::IconState::Dis);
            const save::model::Ride& ride = scene.rides.rides.at(*b.ride);
            v.can_demolish = ride.track && !ride.track->pieces.empty();
            v.can_complete = v.can_demolish && b.tip.has_value() && !b.backward && !opening(ride) && !b.selected;
            v.complete_dim = !b.proposed.empty() && std::fmod(b.clock, 2 * kFlashSeconds) >= kFlashSeconds;
            v.heights = b.heights;
            v.heights_locked = scene.show_ride_heights;
            const rct3::park::track::TrackRules rules = *b.ride < scene.track_rules.size() ? scene.track_rules[*b.ride] : rct3::park::track::TrackRules { };
            if (rules.car_facing)
                v.facing = b.facing;
            // A ride whose track may lie in several stretches, or a water transport, can begin another.
            if (rules.may_be_split || ride.mode == save::model::OperatingMode::WaterTransport)
                v.new_segment = v.can_demolish && !opening(ride) && !b.backward && !b.selected;
            if (b.split != rct3::park::track::SplitState::None)
                v.split = rct3::park::track::split_joined(b.split) ? 0 : rct3::park::track::split_side(b.split);
            const rct3::park::track::PieceCatalogue* c = catalogue(b);
            if (c == nullptr || !b.tip)
                return v;
            const rct3::park::track::ChoiceOptions o = rct3::park::track::choice_options(c->pieces, b.candidates, b.choice);
            const bool by_shape = !b.choice.named.has_value();
            for (int curve = -rct3::park::track::kCurveSizes; curve <= rct3::park::track::kCurveSizes; ++curve)
                v.curves.at(rct3::park::track::curve_index(curve)) = state_of(by_shape && curve == b.choice.curve, o.curves.at(rct3::park::track::curve_index(curve)));
            for (int slope = -rct3::park::track::kSlopeSteps; slope <= rct3::park::track::kSlopeSteps; ++slope)
                v.slopes.at(rct3::park::track::slope_index(slope)) = state_of(by_shape && slope == b.choice.slope, o.slopes.at(rct3::park::track::slope_index(slope)));
            for (std::size_t i = 0; i < ui::screens::build::construction_panel::kBanks; ++i)
            {
                const int bank = panel_bank(i);
                v.banks.at(i) = state_of(by_shape && bank == b.choice.bank, o.banks.at(rct3::park::track::bank_index(bank)));
            }
            for (std::size_t i = 0; i < rct3::park::track::kPieceToggles; ++i)
            {
                const std::uint8_t bit = rct3::park::track::toggle_bit(static_cast<rct3::park::track::PieceToggle>(i));
                if ((o.any_toggles & bit) != 0)
                    v.toggles.at(i) = state_of((b.choice.toggles & o.toggles & bit) != 0, (o.toggles & bit) != 0);
            }
            for (ListedPiece& l : listed_pieces(*assets.strings, *c, rules, o))
            {
                if (l.laid && b.choice.named == l.laid)
                    v.named_on = v.named.size();
                v.named.push_back(std::move(l.shown));
            }
            if (b.picked)
            {
                const rct3::park::track::PieceShape& shape = c->pieces[b.picked->index];
                v.piece = piece_name(*assets.strings, shape.record.name);
                if (!b.ghost || b.clashes)
                    v.piece = assets.strings->get(b.unowned ? kNotOwned : b.in_ground ? kInGround : kCannotPlace);
                else if (b.off_water)
                    v.piece = assets.strings->get("PlaceObject_MustBeOnWater");
                if (!b.complaint.empty())
                {
                    // Some say which ride they are about.
                    v.piece = assets.strings->get(b.complaint);
                    constexpr std::u16string_view kNameMark = u"%NAME%";
                    if (const auto at = v.piece.find(kNameMark); at != std::u16string::npos)
                        v.piece.replace(at, kNameMark.size(), ui::screens::info::attraction_name(scene.rides.rides.at(*b.ride).base, *assets.strings));
                }
                // The price the piece's scenery item stores, in hundredths as the park's money is.
                v.cost = std::u16string(assets.strings->get(kCostLabel)) + u" " + ui::money_text(ui::currency(*assets.strings, currency), shape.record.cost);
                // The settings the piece is built with. A brake's or block brake's speed is stored in whole m/s and
                // shown in tenths of the player's unit, a step a metre a second; the ride's for its kind until set.
                // shortcut: nothing holds a train for the time or turns the cars yet, upgrade with holding brakes
                // and turning cars.
                using enum ui::screens::build::PieceParam;
                if (shape.record.role == rct3::park::track::kRoleBrakes || shape.record.role == rct3::park::track::kRoleBlockBrake)
                {
                    const float ride_speed = shape.record.role == rct3::park::track::kRoleBrakes ? ride.options.brake_speed : ride.options.block_brake_off_speed;
                    const float speed = b.brake_speed > 0 ? static_cast<float>(b.brake_speed) : std::round(ride_speed);
                    const auto tenths = [&](float metres) { return static_cast<int>(std::lround(metres * ui::screens::info::speed_scale(units) * 10)); };
                    v.params.at(std::to_underlying(Speed)) = ui::screens::build::ParamBox { .value = tenths(speed), .least = tenths(1), .most = tenths(kMostBrakeSpeed), .decimals = 1, .unit = std::u16string(ui::screens::info::speed_unit(units)), .step = std::max(tenths(1), 1) };
                }
                if (shape.record.role == kHoldingRole)
                    v.params.at(std::to_underlying(HoldingTime)) = ui::screens::build::ParamBox { .value = b.holding_time, .most = kMostHoldingTime };
                if (rules.car_rotation)
                    v.params.at(std::to_underlying(CarTurn)) = ui::screens::build::ParamBox { .value = b.car_turn, .least = -kMostCarTurn, .most = kMostCarTurn };
            }
            return v;
        }

    } // namespace

    namespace
    {
    } // namespace

    void close_track_building(park::ParkScene& scene)
    {
        if (scene.building.ride)
            ghost_trains(scene, *scene.building.ride, false);
        // A ride left testing or open that the park does not run yet, as an unfinished one tested here, runs its own
        // trains now; those of one it runs stop being ghosts.
        const auto ride = scene.building.ride;
        const bool run = ride && *ride < scene.ride_entities.size() && scene.ride_entities[*ride] != entt::null;
        if (ride && !run && scene.rides.rides.at(*ride).base.state != save::model::AttractionState::Closed && !std::ranges::contains(scene.readying, *ride))
            scene.readying.push_back(*ride);
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
        if (auto placing = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(park::kPlacingName))
        {
            placing->set_body_size(0, 0);
            placing->set_paper(false);
        }
        if (auto arm = scene.desktop->root().find(kArmName))
            arm->close();
        if (auto design = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(park::kDesignPlacingName); design && design->showing())
            design->hide();
        scene.building.design_tabs_for.reset();
        if (auto browser = scene.desktop->root().find(kBrowserName))
            browser->close();
        scene.building.offer_designs = false;
        scene.building.design_shown.reset();
        if (auto placing = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(park::kPlacingName); placing && placing->showing() && !ride_placing(scene))
        {
            placing->hide();
            placing->show_view_text({ });
            scene.placing.preview_mesh = { };
        }
    }

    std::optional<std::string> ride_track_overlay(const park::ParkScene& scene, std::size_t ride)
    {
        if (ride >= scene.rides.rides.size() || !scene.rides.rides[ride].track)
            return std::nullopt;
        const auto objects = object_indices(scene);
        for (const save::model::TrackPiece& piece : scene.rides.rides[ride].track->pieces)
        {
            if (const auto found = objects.find(piece.scenery); found != objects.end() && !scene.objects[found->second].overlay.empty())
                return scene.objects[found->second].overlay;
        }
        return std::nullopt;
    }

    std::int64_t track_cost(park::ParkScene& scene, std::size_t ride, const ui::InterfaceAssets& assets)
    {
        const auto overlay = ride_track_overlay(scene, ride);
        const auto c = overlay ? load_catalogue(scene.building, *overlay, assets) : std::nullopt;
        std::int64_t cost = 0;
        for (const save::model::TrackPiece& piece : c ? scene.rides.rides[ride].track->pieces : std::vector<save::model::TrackPiece> { })
            if (const auto shape = rct3::park::track::shape_named(*c, piece.symbol))
                cost += shape->record.cost;
        return cost;
    }

    bool new_tracked_ride(park::ParkScene& scene, const rct3::park::BuildItem& item, const ui::InterfaceAssets& assets)
    {
        if (item.kind != rct3::park::BuildKind::TrackedRide || !assets.install)
            return false;
        const auto file = assets.install->find(std::format("{}.common.ovl", item.overlay));
        if (!file)
            return false;
        const auto overlay = ovl::Overlay::load(*file);
        if (!overlay)
            return false;
        // The first overlay the ride's file names that holds pieces.
        std::optional<std::string> pieces;
        for (std::string named : rct3::park::track::named_overlays(*overlay, item.overlay))
        {
            if (scene.building.catalogues.contains(named))
                pieces = std::move(named);
            else if (const auto found = assets.install->find(std::format("{}.common.ovl", named)))
            {
                if (auto loaded = rct3::park::track::load_piece_catalogue(*found, named); loaded && !loaded->pieces.empty())
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
        save::model::Ride& ride = scene.rides.rides.emplace_back();
        ride.kind = save::model::RideKind::Tracked;
        ride.base.id = last_id(scene) + 1;
        ride.base.symbol = std::format("{}:trr", item.symbol);
        ride.base.overlay_path = item.overlay;
        const auto colours = rct3::park::name_colours(item.symbol);
        ride.car_colours = { static_cast<std::uint8_t>(colours[0]), static_cast<std::uint8_t>(colours[1]), static_cast<std::uint8_t>(colours[2]) };
        // Its kind's name and a number, as the game names a new ride.
        ride.base.name = rct3::park::ride::numbered_name(scene.rides.rides, ride.base.symbol);
        // Built closed, as in the game: the player tests it and opens it from its window.
        ride.base.state = save::model::AttractionState::Closed;
        ride.base.flags = save::model::AttractionCommon::kNeverOpened;
        take_fence(scene, assets, ride, item);
        ride.track.emplace().segments.resize(1);
        // One train of the first the ride's file lists, seated as its car models are; its window changes them
        // until it opens.
        std::string_view train_name;
        for (const auto part : { core::maybe(&overlay->common()), overlay->unique() })
            for (const ovl::TrackedRideItem& t : part ? ovl::read_tracked_rides(*overlay, *part) : std::vector<ovl::TrackedRideItem> { })
                if (strings::equals_ignoring_case(t.name, item.symbol) && t.ride && t.ride->operating)
                {
                    if (!t.ride->operating->trains.empty())
                        train_name = t.ride->operating->trains.front();
                    scene.train_choices[index].assign(t.ride->operating->trains.begin(), t.ride->operating->trains.end());
                    const ovl::RideOperatingDefaults& o = *t.ride->operating;
                    ride.platform_type = o.platform;
                    ride.mode = static_cast<save::model::OperatingMode>(o.mode);
                    // Its speeds as its file sets them, which the station pushes and the lift pulls trains at.
                    ride.options.launch_speed = o.launch_speed;
                    ride.options.lift_speed = o.lift_speed;
                    ride.options.brake_speed = o.brake_speed;
                    ride.options.block_brake_off_speed = o.block_brake_speed;
                    ride.options.fixed_speed = o.fixed_speed.value_or(-1);
                }
        // Its riders sit on arms.
        const bool arm = !train_name.empty() && use_train(scene, *assets.install, index, train_name).value_or(false);
        // Its platforms come from the folder of tracks/Platforms that has its type.
        std::error_code error;
        const auto platform_folders = assets.install->find("Tracks/Platforms");
        for (auto it = std::filesystem::directory_iterator(platform_folders.value_or(std::filesystem::path { }), error); !ride.platform_type.empty() && !error && it != std::filesystem::directory_iterator(); it.increment(error))
            if (it->is_directory(error) && assets.install->find(std::format("Tracks/Platforms/{}/{}.common.ovl", it->path().filename().string(), ride.platform_type)))
                ride.platform_style = it->path().filename().string();
        if (ride.platform_style.empty())
            ride.platform_type.clear();
        if (!ride.trains.empty())
            logging::logger("park").info("new {} runs {} with {} seats, lift {} m/s", item.symbol, train_name, ride.seats.size(), ride.options.lift_speed);
        // The simulation takes it on once its entrance and exit are placed beside its station (ride_placing.hpp).
        scene.track_rules.resize(scene.rides.rides.size());
        scene.track_rules[index] = rct3::park::track::load_track_rules(*file, ride.base.symbol);
        scene.courses.resize(scene.rides.rides.size());
        logging::logger("park").info("new {} built from {}", item.symbol, *pieces);
        const bool opened = open_track_building(scene, index, *pieces, assets);
        scene.building.arm = scene.building.arm || arm;
        // The saved designs of its kind, offered in their window when it was picked from a list.
        scene.building.designs = designs_for(scene.build, item.symbol);
        scene.building.offer_designs = std::exchange(scene.build.offer_designs, false) && opened && !scene.building.designs.empty();
        return opened;
    }

    void update_platforms(park::ParkScene& scene, std::size_t ride_index, const ui::InterfaceAssets& assets)
    {
        save::model::Ride& ride = scene.rides.rides.at(ride_index);
        const auto overlay = ride_track_overlay(scene, ride_index);
        const auto c = overlay ? load_catalogue(scene.building, *overlay, assets) : std::nullopt;
        const auto kit = c && ride.track ? ride_platforms(scene.building, ride, assets) : std::nullopt;
        if (!kit)
            return;
        const std::string& platforms = kit->overlay;
        auto& pieces = ride.track->pieces;
        // A pool ride's station has no doors; where the complex's paving meets a side of it, its platform shows
        // steps down that side as it would for a door there.
        std::vector<rct3::park::ride::DoorSpot> doors = kit->doors;
        if (ride_index < scene.track_rules.size() && scene.track_rules[ride_index].pool_ride)
            for (const save::model::TrackPiece& p : pieces)
                for (std::int32_t q = 0; q < 4; ++q)
                    if (const rct3::park::Side side = rct3::park::Side::from(q); rct3::park::pools::slide_platform_joined(scene.pools, { .col = p.tile_x, .row = p.tile_z, .height = p.height, .turn = rct3::park::ModelTurn::from(p.direction) }, side))
                    {
                        const auto [dc, dr] = side.step();
                        doors.push_back({ .col = p.tile_x + dc, .row = p.tile_z + dr, .turn = rct3::park::object::turn_facing(side.opposite()), .height = p.height });
                    }
        const auto station = [&](std::size_t i) {
            const auto shape = rct3::park::track::shape_named(*c, pieces[i].symbol);
            return pieces[i].platform.has_value() || (shape && has_platform(*shape));
        };
        wait_for_build(scene);
        bool changed = false;
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
            if (!station(i))
                continue;
            const auto shape = rct3::park::track::shape_named(*c, pieces[i].symbol);
            if (!shape)
                continue;
            // shortcut: the open end being built on counts as the track's end, which only the aquarium's platforms
            // show; upgrade by leaving that end open while the construction panel is on it.
            const rct3::park::ride::PlatformPlace place = platform_place(*shape, i > 0 ? station(i - 1) : ride.track->circuit && station(pieces.size() - 1), i + 1 < pieces.size() ? station(i + 1) : ride.track->circuit && station(0), i > 0 || ride.track->circuit, i + 1 < pieces.size() || ride.track->circuit);
            const auto name = rct3::park::ride::platform_symbol(ride.platform_type, pieces[i], place, doors, kit->names);
            if (!name)
            {
                // Its type has no platform for it there, as some have none without stairs.
                hide_platform(scene, pieces[i]);
                changed = changed || pieces[i].platform.has_value();
                continue;
            }
            const auto objects = object_indices(scene);
            const auto piece_object = objects.find(pieces[i].scenery);
            auto platform = pieces[i].platform ? std::ranges::find_if(scene.objects, [&](const save::model::PlacedObject& o) { return o.owner_ref == pieces[i].platform->id && o.id != pieces[i].platform->id; }) : scene.objects.end();
            if (platform == scene.objects.end())
            {
                if (piece_object == objects.end())
                    continue;
                // Stands where its piece does.
                save::model::PlacedObject o = scene.objects[piece_object->second];
                const std::uint64_t id = last_id(scene) + 1;
                pieces[i].platform = save::model::TrackPlatform { .id = id, .tile_x = pieces[i].tile_x, .tile_z = pieces[i].tile_z, .height = pieces[i].height, .direction = pieces[i].direction };
                o.id = id + 1;
                o.owner_ref = id;
                o.overlay = platforms;
                o.symbol.clear();
                scene.objects.push_back(std::move(o));
                platform = scene.objects.end() - 1;
            }
            if (platform->symbol == *name && platform->overlay == platforms && !platform->hidden)
                continue;
            // A new train may bring another platform type.
            platform->overlay = platforms;
            platform->symbol = *name;
            platform->hidden = false;
            (void)load_object_model(scene, scene.building.models, assets, *platform);
            changed = true;
        }
        if (changed)
            objects_changed(scene);
    }

    bool track_building_click(park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size)
    {
        TrackBuilding& b = scene.building;
        if (!b.ride)
            return false;
        b.completing.reset();
        b.proposed.clear();
        save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        if (c == nullptr || !ride.track)
            return false;
        if (!opening(ride))
        {
            if (size.x <= 0 || size.y <= 0)
                return false;
            const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
            if (scene.pick_boxes_of != scene.objects.size())
                place_objects(scene);
            // Only its pieces, not its doors or platforms.
            std::unordered_set<std::uint64_t> pieces;
            for (const save::model::TrackPiece& p : ride.track->pieces)
                pieces.insert(p.scenery);
            const auto hit = rct3::park::object::pick_object(ray, scene.pick_boxes, scene.grid, [&](std::size_t i) { return !scene.objects[i].hidden && pieces.contains(scene.objects[i].id); });
            const auto piece = hit ? std::ranges::find(ride.track->pieces, scene.objects[hit->index].id, &save::model::TrackPiece::scenery) : ride.track->pieces.end();
            if (piece == ride.track->pieces.end())
                return false;
            // A piece at an open end goes back to building at the track's open end.
            // shortcut: only the last stretch's far end is built on, upgrade by building on from the end clicked.
            const std::size_t index = static_cast<std::size_t>(piece - ride.track->pieces.begin());
            const auto laid = laid_piece(*c, *piece);
            b.selected = laid && rct3::park::track::piece_neighbours(*ride.track, index) ? std::optional(index) : std::nullopt;
            b.complaint.clear();
            if (!b.selected)
                use_split(scene, b, b.split);
            else if (b.split != rct3::park::track::SplitState::None)
                b.split = rct3::park::track::split_of(c->pieces[laid->index].record);
            if (b.selected)
            {
                // Building steps on toward the end of the piece nearer the click.
                const save::model::PlacedObject& o = scene.objects[hit->index];
                const rct3::park::track::PieceShape& shape = c->pieces[laid->index];
                const glm::vec3 point = ray.origin + ray.direction * hit->distance;
                b.selected_back = glm::length(point - rct3::park::track::start_of(o, shape, laid->reversed, scene.grid).position) < glm::length(point - rct3::park::track::tip_of(o, shape, laid->reversed, scene.grid).position);
                b.choice = rct3::park::track::choice_of(c->pieces, *laid);
            }
            refresh(scene, b, assets);
            return true;
        }
        if (!b.ghost)
            return false;
        // A design that may not stand where its ghost is stays in hand, none of it laid.
        if (!b.design.empty() && (b.clashes || b.off_water))
            return true;
        // A design is laid whole, piece after piece from the first, and paid for whatever the park has, then
        // its doors where they stood beside its stations. Its price is its pieces': the only scenery designs
        // keep is the style's standard entrance and exit, which their files price at nothing.
        const std::vector<save::model::TrackPiece> design = std::exchange(b.design, { });
        const std::vector<TrackBuilding::DesignDoor> doors = std::exchange(b.design_doors, { });
        const std::optional<save::model::Ride> settings = std::exchange(b.design_ride, std::nullopt);
        const std::int64_t cash = scene.park.cash.value_or(0);
        // shortcut: a design's pieces are not checked against each other, as it was built whole in the game and
        // our rule refuses some of its pieces that pass close; check them too once the rule matches the game's.
        rct3::park::path::SolidTiles standing;
        if (!design.empty())
        {
            std::vector<save::model::PlacedObject> others;
            std::ranges::copy_if(scene.objects, std::back_inserter(others), [&](const save::model::PlacedObject& o) {
                const auto of = scene.attractions.find(o.id);
                return of == scene.attractions.end() || of->second.kind != rct3::park::AttractionRef::Kind::Ride || of->second.index != *b.ride;
            });
            standing = rct3::park::path::solid_tiles(others, scene.models, scene.grid);
        }
        const save::model::PlacedObject first = b.ghost->object;
        // A design of two tracks side by side holds both halves of each joined piece, laid as they come rather
        // than each laying its other half.
        const rct3::park::track::SplitState split = design.empty() ? b.split : std::exchange(b.split, rct3::park::track::SplitState::None);
        for (std::size_t i = 0; i < std::max<std::size_t>(design.size(), 1); ++i)
        {
            if (i > 0)
            {
                auto placed = design_piece(*c, design, i, first, scene.grid);
                if (!placed || !load_object_model(scene, b.models, assets, placed->object))
                    break;
                b.picked = rct3::park::track::LaidPiece { static_cast<std::size_t>(&*rct3::park::track::shape_named(*c, design[i].symbol) - c->pieces.data()), design[i].reversed };
                // Laid in the design's own order, even on a ride built back from its station.
                b.backward = false;
                // Refused where it would clash with what stands, as a piece built by hand is; the flag left by the
                // last refresh is the offered piece's, not this one's.
                b.clashes = clashes(scene, standing, placed->object);
                b.ghost = std::move(*placed);
            }
            const std::size_t laid = ride.track->pieces.size();
            build(scene, b, assets, !design.empty());
            if (ride.track->pieces.size() == laid)
                break;
            if (!design.empty())
            {
                save::model::TrackPiece& piece = ride.track->pieces.back();
                piece.arm_angles = design[i].arm_angles;
                piece.brake_speed = design[i].brake_speed;
                piece.holding_time = design[i].holding_time;
                piece.speed_class = design[i].speed_class; // A booster's speed among them
            }
        }
        // The height Shift gave the first piece goes with it; a track begun again starts on the ground.
        if (!ride.track->pieces.empty())
        {
            b.raised.reset();
            b.lift.reset();
        }
        if (!design.empty() && ride.track->pieces.size() == design.size())
        {
            // Its stretches as the design has them, as the two tracks of a ride of two, each closed round.
            if (settings && settings->track)
            {
                save::model::Track& track = *ride.track;
                track.segments = settings->track->segments;
                for (save::model::TrackSegment& stretch : track.segments)
                    stretch.id = 0;
                for (std::size_t i = 0; i < design.size(); ++i)
                    track.pieces[i].segment = design[i].segment;
                if (!std::exchange(track.circuit, settings->track->circuit) && track.circuit && !(*b.ride < scene.track_rules.size() && scene.track_rules[*b.ride].pool_ride))
                    scene.doors_requested = *b.ride;
                track_changed(scene, b, assets);
            }
            // It runs as the design was set to run, in the design's colours.
            if (settings)
            {
                ride.mode = settings->mode;
                ride.options = settings->options;
                // Its trains as many and as long as the design's, as far as its kind and its track allow.
                if (const auto limits = scene.train_limits.find(*b.ride); limits != scene.train_limits.end() && settings->train_count > 0 && settings->cars_per_train > 0)
                    b.arm = fit_trains(scene, *assets.install, *b.ride, settings->train_count, std::clamp(settings->cars_per_train, limits->second.fewest_cars, limits->second.most_cars)) || b.arm;
                ride.car_colours = settings->car_colours;
                ride.fence_colours = settings->fence_colours;
                ride.queue_colours = settings->queue_colours;
            }
            // Moved and turned as the first piece was.
            for (const TrackBuilding::DesignDoor& door : doors)
            {
                save::model::PlacedObject o = moved_door(door.object, design.front(), ride.track->pieces.front());
                if (load_object_model(scene, b.models, assets, o))
                    place_station_door(scene, assets, *b.ride, door.station, o, *o.base_height, door.exit);
            }
        }
        if (!design.empty())
        {
            use_split(scene, b, split);
            logging::logger("park").info("design laid with {} of {} pieces for {}, mode {}, lift {} m/s", ride.track->pieces.size(), design.size(), cash - scene.park.cash.value_or(0), std::to_underlying(ride.mode), ride.options.lift_speed);
            // Named and playing as its panel says; then, as a flat ride set down, on to its window and its doors,
            // the entrance first, where the design brought none. A ride in a pool has none to place.
            if (b.design_title.find_first_not_of(u' ') != std::u16string::npos)
                ride.base.name = strings::to_utf8(b.design_title);
            scene.ride_tunes[ride.base.id] = b.design_tune;
            scene.info_requested = rct3::park::AttractionRef { rct3::park::AttractionRef::Kind::Ride, *b.ride };
            if (!(*b.ride < scene.track_rules.size() && scene.track_rules[*b.ride].pool_ride))
                scene.doors_requested = *b.ride;
            close_track_building(scene);
            return true;
        }
        // A ride that is one building is done once set down: on to its window, where its doors are placed.
        if (*b.ride < scene.track_rules.size() && scene.track_rules[*b.ride].one_piece && !ride.track->pieces.empty())
        {
            scene.info_requested = rct3::park::AttractionRef { rct3::park::AttractionRef::Kind::Ride, *b.ride };
            close_track_building(scene);
        }
        return true;
    }

    bool open_track_building(park::ParkScene& scene, std::size_t ride, std::string overlay, const ui::InterfaceAssets& assets)
    {
        if (ride >= scene.rides.rides.size() || !scene.rides.rides[ride].track || !assets.install)
            return false;
        TrackBuilding& b = scene.building;
        if (!load_catalogue(b, overlay, assets))
            return false;
        // Golf paths are never banked, and their pieces list the path's edges either way round, which reads as
        // upside down against the station.
        if (scene.rides.rides[ride].mode == save::model::OperatingMode::MiniGolf)
            for (rct3::park::track::PieceShape& p : b.catalogues.at(overlay).pieces)
                p.entry.bank = p.exit.bank = 0;
        b.ride = ride;
        b.overlay = std::move(overlay);
        b.choice = { };
        b.station_first = true;
        b.selected.reset();
        b.aim.reset();
        b.turn = { };
        b.raised.reset();
        b.lift.reset();
        b.tip = track_tip(scene, b);
        b.arm = std::ranges::any_of(scene.arm_cars, [&](const park::ParkScene::ArmCar& a) { return scene.car_spots[a.car].ride == ride; });
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
        use_split(scene, b, std::ranges::any_of(pieces, [](const rct3::park::track::PieceShape& p) { return p.record.side != 0; }) ? rct3::park::track::SplitState::JoinedLeft : rct3::park::track::SplitState::None);
        refresh(scene, b, assets);
        auto placing = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(park::kPlacingName);
        if (!placing)
            return false;
        if (!scene.desktop->root().find(kPanelName))
        {
            auto& panel = placing->body().add<ui::screens::build::ConstructionPanel>("construction", ui::game_skins::construction_panel_style(assets.palette), std::array { std::u16string(assets.strings->get(kToggleHelps[0])), std::u16string(assets.strings->get(kToggleHelps[1])), std::u16string(assets.strings->get(kToggleHelps[2])), std::u16string(assets.strings->get(kToggleHelps[3])), std::u16string(assets.strings->get(kToggleHelps[4])) }, std::u16string(assets.strings->get(kDemolishHelp)));
            panel.placement = ui::top_left(-kControlsLeft, -kControlsTop, ui::screens::build::construction_panel::kWidth, ui::screens::build::construction_panel::kHeight);
            panel.complete_button().tooltip = assets.strings->get(kCompleteHelp);
            panel.new_segment_button().tooltip = assets.strings->get(kNewSegmentHelp);
            panel.split_button().tooltip = assets.strings->get(kSplitHelp);
            panel.heights_help = assets.strings->get("ATE_TT_HeightMarks");
            panel.facing_button().tooltip = assets.strings->get("ATE_TT_SpinAngle");
            using enum ui::screens::build::PieceParam;
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
        void update_arm_strip(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
        {
            namespace skins = ui::game_skins;
            auto& pieces = scene.rides.rides.at(*b.ride).track->pieces;
            auto strip = scene.desktop->root().find_as<ui::widgets::Panel>(kArmName);
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
                strip = scene.desktop->root().add<ui::widgets::Panel>(std::string(kArmName), std::string(skins::kCurvedStrip));
                strip->placement = ui::bottom_left(ui::screens::build::construction_panel::kStripLeft, kPanelTop, 540, kRow + 8);
                auto& enable = strip->add<ui::widgets::Tickbox>("enable", std::string(skins::kTickbox), std::string(skins::kTick));
                enable.tooltip = assets.strings->get(kArmEnableHelp);
                enable.placement = ui::top_left(8, 6, 20, 20);
                auto& label = strip->add<ui::widgets::Label>("enable_label", std::u16string(assets.strings->get(kArmEnable)));
                label.placement = ui::top_left(30, 6, 60, 20);
                float left = 92;
                for (const auto& [name, text, help] : { std::tuple { "zero", u"0", kArmZeroHelp }, { "copy_prev", u"<", kArmPrevHelp }, { "copy_next", u">", kArmNextHelp } })
                {
                    auto& button = strip->add<ui::widgets::Button>(name, std::string(skins::kTextButton));
                    button.text = text;
                    button.tooltip = assets.strings->get(help);
                    button.placement = ui::top_left(left, 5, 22, 22);
                    left += 24;
                }
                for (const std::string_view joint : kArmJoints)
                {
                    auto& spinner = strip->add<ui::widgets::Spinner>(std::string(joint), skins::spinner_style(assets.palette));
                    spinner.minimum = -kArmTurns;
                    spinner.maximum = kArmTurns;
                    spinner.step = kArmStep;
                    spinner.tooltip = assets.strings->get(kArmAngleHelp);
                    spinner.placement = ui::top_left(left + 4, 5, kSpinner, 22);
                    left += kSpinner + 4;
                }
                for (const auto& [name, text, help] : { std::tuple { "piece_back", u"<<", kPieceBackHelp }, { "piece_forward", u">>", kPieceForwardHelp } })
                {
                    auto& button = strip->add<ui::widgets::Button>(name, std::string(skins::kTextButton));
                    button.text = text;
                    button.tooltip = assets.strings->get(help);
                    button.placement = ui::top_left(left + 4, 5, 26, 22);
                    left += 30;
                }
                auto& at = strip->add<ui::widgets::Label>("piece_label", std::u16string { });
                at.placement = ui::top_left(left + 6, 6, 60, 20);
                b.arm_piece = 0;
            }
            // Counted back from the last piece, so a piece laid while the last is shown is shown next.
            if (strip->find_as<ui::widgets::Button>("piece_back")->take_click())
                ++b.arm_back;
            if (strip->find_as<ui::widgets::Button>("piece_forward")->take_click() && b.arm_back > 0)
                --b.arm_back;
            b.arm_back = std::min(b.arm_back, pieces.size() - 1);
            const std::size_t index = pieces.size() - 1 - b.arm_back;
            save::model::TrackPiece& piece = pieces[index];
            strip->find_as<ui::widgets::Label>("piece_label")->text = strings::to_utf16(std::format("{}/{}", index + 1, pieces.size()));
            auto& enable = *strip->find_as<ui::widgets::Tickbox>("enable");
            // The nearest piece before or after this one (round a circuit) with angles of its own.
            const auto nearest = [&](bool before) -> std::optional<rct3::park::track::ArmAngles> {
                for (std::size_t k = 1; k < pieces.size(); ++k)
                    if (const auto& other = pieces[before ? (index + pieces.size() - k) % pieces.size() : (index + k) % pieces.size()]; other.arm_angles)
                        return other.arm_angles;
                return std::nullopt;
            };
            bool shown = b.arm_piece == piece.id;
            if (enable.take_change())
            {
                piece.arm_angles = enable.ticked ? nearest(true).value_or(rct3::park::track::ArmAngles { }) : std::optional<rct3::park::track::ArmAngles> { };
                shown = false;
            }
            for (const auto& [name, before] : { std::pair { "copy_prev", true }, { "copy_next", false } })
                if (strip->find_as<ui::widgets::Button>(name)->take_click())
                    if (const auto copied = nearest(before))
                    {
                        piece.arm_angles = copied;
                        shown = false;
                    }
            if (strip->find_as<ui::widgets::Button>("zero")->take_click())
            {
                piece.arm_angles = rct3::park::track::ArmAngles { };
                shown = false;
            }
            for (std::size_t j = 0; j < kArmJoints.size(); ++j)
            {
                auto& spinner = *strip->find_as<ui::widgets::Spinner>(kArmJoints[j]);
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

    void update_track_building(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, int currency, ui::screens::info::DistanceUnits units, ui::Point at, ui::Point size, bool over, bool shift, std::span<const ui::PlayCommand> commands, float elapsed)
    {
        TrackBuilding& b = scene.building;
        b.clock += elapsed;
        auto browser = scene.desktop->root().find_as<ui::screens::build::DesignBrowser>(kBrowserName);
        if (b.design_picture && !browser)
        {
            // The design's picture goes with its window.
            device.destroy_texture(*b.design_picture);
            b.design_picture.reset();
        }
        auto panel = scene.desktop->root().find_as<ui::screens::build::ConstructionPanel>(kPanelName);
        auto placing = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(park::kPlacingName);
        if (!panel || !placing || !b.ride)
            return;
        if (placing->take_closed())
        {
            close_track_building(scene);
            return;
        }
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        bool changed = false;
        // The round panel's ring, in this order: the ride's window, turning the first piece, the construction
        // strip, its state, the colours of the pieces built next, and cutting the ground smoothly or sharply, one
        // of the two always lit. The turn shows greyed while it does nothing.
        enum Tab : std::size_t
        {
            Info,
            Rotate,
            Construction,
            State,
            Colour,
            Smooth,
            Sharp,
        };
        // A ride set down whole has no strip to show.
        const bool one_piece = *b.ride < scene.track_rules.size() && scene.track_rules[*b.ride].one_piece;
        const bool starting = opening(scene.rides.rides.at(*b.ride));
        if (const auto tab = placing->take_action())
        {
            if (*tab == Info)
                scene.info_requested = rct3::park::AttractionRef { rct3::park::AttractionRef::Kind::Ride, *b.ride };
            // Each press turns the first piece a quarter, as a flat ride's turn does.
            if (*tab == Rotate && starting)
            {
                b.turn = b.turn.turned(1);
                changed = true;
            }
            // Each press shows or hides the strip of construction controls.
            if (*tab == Construction && !one_piece)
                panel->visible = !panel->visible;
            if (*tab == Smooth || *tab == Sharp)
                b.smooth_cuts = *tab == Smooth;
        }
        const bool whole = !panel->visible;
        if (const auto paint = placing->take_paint(); paint && paint->pot < std::tuple_size_v<save::model::RideColours>)
        {
            save::model::RideColours colours = next_colours(scene, b);
            colours.at(paint->pot) = static_cast<std::uint8_t>(paint->colour);
            b.colours = colours;
            b.tabs_for.reset();
        }
        const rct3::park::AttractionRef ref { .kind = rct3::park::AttractionRef::Kind::Ride, .index = *b.ride };
        const std::vector<ui::screens::info::StateChoice> states = park::state_choices(scene, ref);
        if (const auto choice = placing->take_choice(); choice && choice->first == State && choice->second < states.size())
        {
            scene.refusal = park::change_ride_state(scene, *b.ride, states[choice->second].state).value_or("");
            b.tabs_for.reset();
        }
        // A design in hand has its own round panel, as a flat ride being placed has, in this order: turning it, with
        // the price of all its pieces beside it; its window's page, its name to type and its tune with why it may
        // not stand where it is; its colours; and cutting the ground it passes into smoothly or sharply. At most one
        // of the two is lit, pressing the lit one putting it out; with neither, ground in its way refuses it.
        const bool holding = starting && !b.design.empty();
        auto design = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(park::kDesignPlacingName);
        if (design && holding)
        {
            enum DesignTab : std::size_t
            {
                Turn,
                Page,
                Paint,
                Smoothly,
                Sharply,
            };
            if (placing->showing())
                placing->hide();
            if (design->take_closed())
            {
                close_track_building(scene);
                return;
            }
            if (const auto tab = design->take_action())
            {
                if (*tab == Turn)
                {
                    b.turn = b.turn.turned(1);
                    changed = true;
                }
                if (*tab == Smoothly || *tab == Sharply)
                {
                    const bool smooth = *tab == Smoothly;
                    b.cut_ground = !(b.cut_ground && b.smooth_cuts == smooth);
                    b.smooth_cuts = smooth;
                    changed = true;
                }
            }
            if (const auto paint = design->take_paint(); paint && paint->pot < std::tuple_size_v<save::model::RideColours>)
            {
                save::model::RideColours colours = next_colours(scene, b);
                colours.at(paint->pot) = static_cast<std::uint8_t>(paint->colour);
                b.colours = colours;
            }
            if (const auto change = design->take_change())
            {
                if (change->id == ui::screens::info::control_id(ui::screens::info::AttractionControl::Name))
                    b.design_title = change->text;
                else if (change->id == ui::screens::info::control_id(ui::screens::info::AttractionControl::Music))
                    b.design_tune = static_cast<std::size_t>(std::max(change->value, 0));
            }
            const auto page = [&] {
                ui::screens::info::InfoPage p;
                ui::screens::info::InfoControl name { .id = ui::screens::info::control_id(ui::screens::info::AttractionControl::Name), .name = "TT_AttractionDialogIconLabelButton", .kind = ui::screens::info::InfoControlKind::Text, .maximum = ui::screens::info::kLongestAttractionName, .text = b.design_title, .width = ui::screens::info::kNameWidth, .help = std::u16string(assets.strings->get("TT_AttractionDialogIconLabelButton")) };
                p.controls.push_back(std::move(name));
                ui::screens::info::InfoControl music { .id = ui::screens::info::control_id(ui::screens::info::AttractionControl::Music), .name = "AttractionViewDialogMusicToolTip", .kind = ui::screens::info::InfoControlKind::Choice, .value = static_cast<int>(b.design_tune), .beside = true, .width = ui::screens::info::kNameWidth, .help = std::u16string(assets.strings->get("AttractionViewDialogMusicToolTip")) };
                music.items.emplace_back(assets.strings->get("AttractionViewDialogNoMusic"));
                music.items.insert(music.items.end(), scene.tunes.begin(), scene.tunes.end());
                p.controls.push_back(std::move(music));
                p.controls.push_back({ .name = "status", .kind = ui::screens::info::InfoControlKind::Words });
                return p;
            };
            const save::model::RideColours colours = next_colours(scene, b);
            if (const std::tuple now { b.cut_ground, b.smooth_cuts, colours }; b.design_tabs_for != now || !design->showing())
            {
                std::vector<ui::screens::info::InfoTab> tabs(5);
                tabs[Turn] = { .icon = "IconAttractionEditorRotate", .help = std::u16string(assets.strings->get("ATE_TT_Rotate")), .action = true };
                for (const ui::screens::info::AttractionTabInfo& t : ui::screens::info::attraction_tabs(ui::screens::info::AttractionKind::Tracked, save::model::AttractionState::Closed))
                    if (t.tab == ui::screens::info::AttractionTab::View)
                        tabs[Page] = { .icon = std::string(t.icon), .help = std::u16string(assets.strings->get(t.help)) };
                tabs[Paint] = { .icon = "Attraction_Colour", .help = std::u16string(assets.strings->get("Scenery_SetFlexiColours")), .paints = { colours.begin(), colours.end() } };
                for (const auto& [tab, name, smooth] : { std::tuple { Smoothly, "Smooth", true }, std::tuple { Sharply, "Sharp", false } })
                    tabs[tab] = { .icon = std::format("TerrainFlattener_{}{}", name, b.cut_ground && b.smooth_cuts == smooth ? "On" : "Off"), .help = std::u16string(assets.strings->get(std::format("TerrainFlattener_{}_ToolTip", name))), .action = true };
                // The turn, the page and the colours where the original's three are; the ground's two down the
                // disc's outer side under them, clear of the page.
                for (std::size_t i = 0; i < tabs.size(); ++i)
                    tabs[i].slot = i < Smoothly ? static_cast<int>(i) : static_cast<int>(Smoothly) - 1 - static_cast<int>(i);
                design->show(std::move(tabs), Page);
                design->show_page(page());
                b.design_tabs_for = now;
            }
            if (const auto picked = design->take_picked(); picked && *picked == Page)
                design->show_page(page());
            if (auto status = design->find_as<ui::widgets::Label>("body/status_label"))
                status->text = assets.strings->get(rct3::park::refusal_key(b.refusal));
            std::int64_t price = 0;
            for (const save::model::TrackPiece& p : b.design)
                if (const auto shape = c != nullptr ? rct3::park::track::shape_named(*c, p.symbol) : std::nullopt)
                    price += shape->record.cost;
            design->show_view_text(ui::money_text(ui::currency(*assets.strings, currency), price));
        }
        else if (design && design->showing())
        {
            design->hide();
            b.design_tabs_for.reset();
        }
        if (const std::tuple now { starting, scene.rides.rides.at(*b.ride).base.state, whole, b.smooth_cuts }; !holding && (b.tabs_for != now || !placing->showing()))
        {
            std::vector<ui::screens::info::InfoTab> tabs(7);
            for (const ui::screens::info::AttractionTabInfo& t : ui::screens::info::attraction_tabs(ui::screens::info::AttractionKind::Tracked, std::get<1>(now)))
            {
                if (t.tab != ui::screens::info::AttractionTab::View && t.tab != ui::screens::info::AttractionTab::State && t.tab != ui::screens::info::AttractionTab::Construction)
                    continue;
                ui::screens::info::InfoTab& tab = tabs[t.tab == ui::screens::info::AttractionTab::View ? Info : t.tab == ui::screens::info::AttractionTab::State ? State : Construction];
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
            }
            tabs[Rotate].icon = starting ? "IconAttractionEditorRotate" : "IconAttractionEditorRotate_Disabled";
            tabs[Rotate].help = assets.strings->get("ATE_TT_Rotate");
            if (one_piece)
                tabs[Construction].icon = "Attraction_Construction_Disabled";
            const save::model::RideColours colours = next_colours(scene, b);
            tabs[Colour].icon = "Attraction_Colour";
            tabs[Colour].help = assets.strings->get("Scenery_SetFlexiColours");
            tabs[Colour].paints = { colours.begin(), colours.end() };
            tabs[State].choices = park::state_fan(scene, ref, *assets.strings);
            for (const auto& [tab, name, lit] : { std::tuple { Smooth, "Smooth", b.smooth_cuts }, std::tuple { Sharp, "Sharp", !b.smooth_cuts } })
            {
                tabs[tab].icon = std::format("TerrainFlattener_{}{}", name, lit ? "On" : "Off");
                tabs[tab].help = assets.strings->get(std::format("TerrainFlattener_{}_ToolTip", name));
            }
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
        if (const save::model::Ride& ride = scene.rides.rides.at(*b.ride); opening(ride))
        {
            // The pointer off the ground, say over the panel, leaves the first piece where it was aimed. With Shift held
            // the piece stays on its tile and rises or sinks with the pointer, as scenery does.
            const auto ray = !over && size.x > 0 && size.y > 0 ? std::optional(rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y })) : std::nullopt;
            if (!shift || !b.ghost || !ray)
                b.lift.reset();
            if (b.lift)
            {
                if (const auto height = lifted_height(*ray, b.lift->at, b.lift->height); height && height != b.raised)
                {
                    b.raised = height;
                    changed = true;
                }
            }
            else if (shift && b.ghost && ray)
            {
                if (const auto distance = rct3::park::object::ground_hit(*ray, scene.grid))
                    b.lift = TrackBuilding::Lift { ray->origin + ray->direction * *distance, b.ghost->object.base_height.value_or(0) };
            }
            else if (const auto pointed = ray ? ground_tile(scene, at, size) : std::nullopt; pointed && pointed != b.aim)
            {
                b.aim = pointed;
                changed = true;
            }
            for (const ui::PlayCommand command : commands)
            {
                if (command == ui::PlayCommand::TurnItemRight || command == ui::PlayCommand::TurnItemLeft)
                {
                    b.turn = b.turn.turned(command == ui::PlayCommand::TurnItemRight ? 1 : -1);
                    changed = true;
                }
            }
        }
        while (const auto event = panel->take_event())
        {
            if (c == nullptr)
                break;
            b.complaint.clear();
            using enum ui::screens::build::ConstructionAction;
            if (event->action != Complete)
            {
                b.completing.reset();
                b.proposed.clear();
            }
            switch (event->action)
            {
                case Curve:
                    b.choice = rct3::park::track::pick_value(c->pieces, b.candidates, b.choice, rct3::park::track::ChoiceAxis::Curve, static_cast<int>(event->index) - rct3::park::track::kCurveSizes);
                    break;
                case Slope:
                    b.choice = rct3::park::track::pick_value(c->pieces, b.candidates, b.choice, rct3::park::track::ChoiceAxis::Slope, static_cast<int>(event->index) - rct3::park::track::kSlopeSteps);
                    break;
                case Bank:
                    b.choice = rct3::park::track::pick_value(c->pieces, b.candidates, b.choice, rct3::park::track::ChoiceAxis::Bank, panel_bank(event->index));
                    break;
                case Toggle:
                    b.choice = rct3::park::track::pick_toggle(c->pieces, b.candidates, b.choice, static_cast<rct3::park::track::PieceToggle>(event->index));
                    break;
                case Named:
                {
                    const rct3::park::track::TrackRules rules = *b.ride < scene.track_rules.size() ? scene.track_rules[*b.ride] : rct3::park::track::TrackRules { };
                    const std::vector<ListedPiece> list = listed_pieces(*assets.strings, *c, rules, rct3::park::track::choice_options(c->pieces, b.candidates, b.choice));
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
                    switch (static_cast<ui::screens::build::PieceParam>(event->index))
                    {
                        case ui::screens::build::PieceParam::CarTurn:
                            b.car_turn = event->value;
                            break;
                        case ui::screens::build::PieceParam::HoldingTime:
                            b.holding_time = event->value;
                            break;
                        case ui::screens::build::PieceParam::Speed:
                            b.brake_speed = std::clamp(static_cast<int>(std::lround(static_cast<float>(event->value) / 10 / ui::screens::info::speed_scale(units))), 1, static_cast<int>(kMostBrakeSpeed));
                            break;
                    }
                    break;
                case Facing:
                    b.facing = static_cast<std::uint8_t>((b.facing + 1) % ui::screens::build::construction_panel::kFacings);
                    break;
                case NewSegment:
                {
                    // shortcut: cars run the stretches as one course, jumping between them, upgrade when the course
                    // follows each stretch on its own.
                    save::model::Track& track = *scene.rides.rides.at(*b.ride).track;
                    track.segments.push_back({ .first_piece = track.pieces.size() });
                    b.selected.reset();
                    b.design.clear();
                    b.design_ride.reset();
                    break;
                }
                case Split:
                    // Over a piece picked mid-track, only the kind of pieces changes.
                    if (b.selected)
                        b.split = rct3::park::track::next_split(b.split, split_closed(scene, b), true);
                    else
                        use_split(scene, b, rct3::park::track::next_split(b.split, split_closed(scene, b), false));
                    break;
            }
            changed = true;
        }
        if (b.completing)
        {
            if (const auto found = rct3::park::track::search_on(*b.completing, kCompletionTriesPerFrame))
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
        // While the panel has the ride, its own trains are ghosts too, so a crash off a track being built is only shown.
        ghost_trains(scene, *b.ride, true);
        // A test of a track that is not yet a circuit, of a ride the park does not run yet: no train runs it, so a
        // ghost of its first train does, crashing off its end. Once the park runs the ride, its own trains test it.
        // Boats sail off the end of theirs instead.
        const save::model::Ride& tested = scene.rides.rides.at(*b.ride);
        const auto& course = *b.ride < scene.courses.size() ? scene.courses[*b.ride] : std::nullopt;
        const bool testable = tested.base.state == save::model::AttractionState::Testing && course && !course->closed && std::ranges::contains(course->kinds, rct3::park::track::CourseKind::Station) && !tested.trains.empty() && tested.mode != save::model::OperatingMode::Boat && tested.mode != save::model::OperatingMode::WaterTransport && scene.car_fits.contains(*b.ride) && assets.install && (*b.ride >= scene.ride_entities.size() || scene.ride_entities[*b.ride] == entt::null);
        if (!testable)
            b.test.reset();
        else if (!b.test)
        {
            // The ride's first train, stood with its front where trains set off. The course is taken as a circuit
            // to make the train, which is then told the track has ends.
            save::model::Ride ghost = tested;
            ghost.trains.resize(1);
            rct3::park::track::TrackCourse round = *course;
            round.closed = true;
            if (auto train = simulation::coaster_of(round, ghost))
            {
                train->closed = false;
                train->ghost = true;
                train->running = true;
                train->speed = train->splash_out ? 0 : train->station_speed; // Sent off by the station, as the park sends a train; a slide's car is eased off
                rct3::park::track::stand_train(*course, ghost.trains.front(), scene.car_fits.at(*b.ride), train->start);
                const rct3::park::ride::RideCars cars = rct3::park::ride::ride_cars(std::span(&ghost, 1), 0);
                park::load_car_models(scene, *assets.install, cars);
                if (const auto model = cars.pieces.empty() ? scene.models.end() : scene.models.find(rct3::park::object::model_ref(cars.pieces.front())); model != scene.models.end())
                    park::fit_car(*train, model->second, *b.ride < scene.track_rules.size() && scene.track_rules[*b.ride].pool_ride);
                TrackBuilding::GhostTrain& t = b.test.emplace(TrackBuilding::GhostTrain { .train = *train, .spots = cars.spots, .stood_at = ghost.trains.front().distance });
                for (const save::model::PlacedObject& car : cars.pieces)
                    t.stood.push_back(cars.fitted.at(car.id));
                t.meshes = rct3::park::moving_meshes(cars.pieces, scene.models, scene.grid);
                for (std::size_t i = 0; i < cars.pieces.size(); ++i)
                    if (const auto model = scene.models.find(rct3::park::object::model_ref(cars.pieces[i])); model != scene.models.end())
                        t.joints.insert(t.joints.end(), model->second.arm_joints.size(), i);
                for (auto* vertices : { &t.meshes.mesh.vertices, &t.meshes.own })
                    for (rct3::park::GroundVertex& v : *vertices)
                        v.colour *= kGhostTint;
                ghost_textures(device, scene, t.meshes.mesh);
            }
        }
        if (b.test)
        {
            TrackBuilding::GhostTrain& t = *b.test;
            if (!t.train.crashed && simulation::drive(t.train, elapsed) && t.train.splashed)
            {
                // Off a pool slide's end, its car flies on down to the ground there.
                const glm::vec3 at = simulation::track_point(t.train, t.train.at);
                const glm::vec3 way = simulation::track_way(t.train, t.train.at);
                t.train.flight = simulation::Flight { .from = at, .way = way, .at = at, .velocity = way * t.train.speed, .water = std::min(scene.grid.height_at(at.x, at.z).value_or(0.0f), at.y) };
            }
            if (t.train.flight && !simulation::fly(*t.train.flight, elapsed))
                t.train.flight.reset();
            if (t.train.crashed && t.wrecks.empty())
                t.wrecks = simulation::thrown_cars(t.train, t.dice);
            // shortcut: wrecks fly through what stands in the park until the park has run, upgrade if a test with
            // the park paused should hit scenery too.
            const auto* standing = scene.sim.registry.ctx().find<simulation::WreckSurroundings>();
            const simulation::WreckSurroundings around = standing != nullptr ? *standing : simulation::WreckSurroundings { .ground = [&scene](glm::vec2 p) { return scene.grid.height_at(p.x, p.y).value_or(0.0f); } };
            for (simulation::Wreck& wreck : t.wrecks)
                if (simulation::fly(wreck, elapsed, around) == simulation::WreckEvent::BlewUp)
                    for (const std::string_view effect : { "CoasterExplosion", "CoasterSmoke" })
                        park::fire_park_particles(scene.particles, effect, wreck.at, glm::vec3(0, 1, 0));
            // Its wrecks gone, or stuck a while short of the end, it is back at the station and sets off again.
            const bool gone = t.train.crashed && std::ranges::all_of(t.wrecks, [](const simulation::Wreck& w) { return w.state == simulation::WreckState::Gone; });
            if (gone || (!t.train.crashed && !t.train.running && !t.train.flight && (t.waited += elapsed) >= kTestWait))
            {
                t.train.crashed = false;
                t.train.splashed = false;
                t.wrecks.clear();
                t.train.at = t.train.start;
                t.train.travelled = 0;
                t.train.lean = t.train.lean_rate = 0;
                t.train.launched = false;
                t.train.running = true;
                t.train.speed = t.train.splash_out ? 0 : t.train.station_speed;
                t.waited = 0;
            }
            std::vector<glm::mat4> places;
            for (std::size_t i = 0; i < t.stood.size(); ++i)
            {
                glm::mat4 place = rct3::park::track::moved_along(*course, t.stood[i], { t.spots[i].front, t.spots[i].rear }, t.train.at - t.stood_at);
                if (t.train.splashed)
                    place = t.train.flight ? simulation::flown(*t.train.flight, place) : glm::mat4(0.0f); // Off a pool slide's end, flying on and afloat till gone
                // Crashed, each car flies and tumbles as its wreck does, and is gone with it.
                if (const simulation::Wreck* wreck = t.train.crashed && i < t.wrecks.size() ? &t.wrecks[i] : nullptr)
                {
                    const glm::vec3 left(place[3]);
                    place = wreck->state == simulation::WreckState::Gone ? glm::mat4(0.0f) : glm::translate(glm::mat4(1.0f), left + wreck->at - wreck->from) * glm::mat4_cast(wreck->turned) * glm::translate(glm::mat4(1.0f), -left) * place;
                }
                places.push_back(place);
            }
            // shortcut: the joints of cars on arms ride at rest with their car, upgrade if a test should swing them
            // as the park does.
            for (const std::size_t car : t.joints)
            {
                const glm::mat4 place = places[car];
                places.push_back(place);
            }
            rct3::park::move_meshes(t.meshes, places);
        }
        if (b.completing)
            show_way(scene, b, assets, rct3::park::track::nearest_way(*b.completing));
        else if (!b.proposed.empty())
            show_way(scene, b, assets, b.proposed);
        ghost_textures(device, scene, b.ghost_mesh);
        panel->show(panel_view(scene, b, assets, currency, units));
        // The disc shows the piece to lay alone, turning, with its price across it.
        std::u16string price;
        if (b.ghost && b.picked && c != nullptr)
        {
            save::model::PlacedObject alone = b.ghost->object;
            alone.col = alone.row = 0;
            alone.base_height = 0;
            alone.force_absolute_height = true;
            if (b.preview_item != alone.symbol)
            {
                const std::array<save::model::PlacedObject, 1> one { alone };
                scene.placing.preview_mesh = rct3::park::object::object_mesh(one, scene.models, scene.grid);
                ghost_textures(device, scene, scene.placing.preview_mesh);
                scene.placing.preview_bones.clear();
                b.preview_item = alone.symbol;
            }
            scene.placing.preview_camera = rct3::park::framed(rct3::park::object::object_corners(alone, scene.models, scene.grid, scene.fitted), kPreviewTurn * scene.clock, kPreviewPitch, kPreviewClosest);
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
            // A design opens by what the career being played has done; outside a career, by the best of them all.
            std::vector<career::Career> careers;
            if (scene.saves.data)
                for (career::CareerEntry& e : career::list_careers(scene.saves.data->folder(config::UserFolder::Careers), 0, std::nullopt))
                    if (scene.game.career.empty() || e.path == scene.game.career)
                        careers.push_back(std::move(e.data.career));
            const auto heading = [&](std::string_view key) {
                std::u16string h(strings.get(key));
                while (!h.empty() && (h.back() == u':' || h.back() == u' '))
                    h.pop_back();
                return h;
            };
            std::vector<ui::widgets::TableColumn> columns { { .heading = heading("Staff_ColumnHeader_Name") }, { .heading = heading("AttractionResultsDialogLabel_Excitement"), .width = 120, .align = ui::Align::End }, { .heading = heading("AttractionResultsDialogLabel_Intensity"), .width = 120, .align = ui::Align::End } };
            std::vector<ui::widgets::TableRow> rows { { .cells = { std::u16string(strings.get(kCustomTrack)), { }, { } }, .keys = { }, .id = 0 } };
            b.designs_locked.clear();
            for (std::size_t i = 0; i < b.designs.size(); ++i)
            {
                const save::model::DesignSummary& d = b.designs[i].summary;
                std::u16string text = design_title(strings, b.designs[i].path);
                const bool locked = career::design_locked(d.scenario, d.level, careers);
                b.designs_locked.push_back(locked);
                if (locked)
                    text += strings.get(kLockedSuffix);
                const auto rated = [&](float v) { return d.assessment.valid ? strings::to_utf16(std::format("{:.2f}", v)) : std::u16string { }; };
                const auto key_of = [&](float v) { return d.assessment.valid ? std::optional<double>(v) : std::nullopt; };
                rows.push_back({ .cells = { std::move(text), rated(d.assessment.excitement), rated(d.assessment.intensity) }, .keys = { std::nullopt, key_of(d.assessment.excitement), key_of(d.assessment.intensity) }, .id = i + 1 });
            }
            browser = scene.desktop->root().add<ui::screens::build::DesignBrowser>(std::string(kBrowserName), ui::game_skins::design_browser_style(assets.palette), std::u16string(strings.get("TrackBrowser_Title")), "Ride_Coaster", std::move(columns), std::move(rows), std::u16string(strings.get("TrackBrowser_BuildThis")), std::u16string(strings.get("TrackBrowser_Cancel")));
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
                        for (const auto part : std::array { core::maybe(&car_file->common()), car_file->unique() })
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
            std::vector<ui::screens::info::InfoLine> lines;
            bool locked = false;
            if (b.design_shown)
            {
                // Read again for its picture, which the list of designs does not keep.
                std::ifstream in(b.designs[*b.design_shown].path, std::ios::binary);
                const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                const auto d = save::model::read_design_summary(std::as_bytes(std::span(raw)));
                const save::model::DesignSummary& summary = d ? *d : b.designs[*b.design_shown].summary;
                if (!summary.picture.empty())
                    if (auto texture = device.create_texture({ summary.picture_width, summary.picture_height, render::TextureFormat::Rgba8, std::as_bytes(std::span(summary.picture)), "design picture" }))
                    {
                        b.design_picture = *texture;
                        picture = ui::texture_id(*texture);
                    }
                lines = ui::screens::info::design_lines(summary, *assets.strings, { ui::currency(*assets.strings, currency), units });
                locked = b.designs_locked.at(*b.design_shown);
                if (locked)
                    lines.push_back({ .label = ui::locked_design_help(*assets.strings, summary.scenario, summary.level), .name = std::string(kLockedHelp) });
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
            b.design = design ? design->ride.track->pieces : std::vector<save::model::TrackPiece> { };
            b.design_ride = design ? std::optional(design->ride) : std::nullopt;
            // Named as its window names it, with no tune, and refused where its ground needs cutting until asked.
            b.design_title = *build > 0 ? design_title(*assets.strings, b.designs.at(*build - 1).path) : std::u16string { };
            b.design_tune = 0;
            b.cut_ground = false;
            b.design_doors.clear();
            for (std::size_t i = 0; design && i < design->ride.stations.size(); ++i)
            {
                for (const bool exit : { false, true })
                {
                    const std::uint64_t id = exit ? design->ride.stations[i].exit : design->ride.stations[i].entrance;
                    if (const auto door = std::ranges::find(design->scenery, id, &save::model::PlacedObject::id); id != 0 && door != design->scenery.end())
                        b.design_doors.push_back({ i, exit, *door });
                }
            }
            if (design && b.design_train < trains.size())
                if (const auto arm = use_train(scene, *assets.install, *b.ride, trains[b.design_train]))
                    b.arm = *arm || b.arm;
            refresh(scene, b, assets);
        }
    }

    rct3::park::TerrainMesh track_arrows(const park::ParkScene& scene)
    {
        const TrackBuilding& b = scene.building;
        if (!b.ride || !b.ghost || !b.tip)
            return { };
        return rct3::park::track_arrows(*b.tip, b.ghost->next, !b.backward);
    }

    void draw_track_ghost(render::Device& device, const park::ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.building.ride)
        {
            draw_ghost_mesh(device, scene, scene.building.ghost_mesh, transform, light);
            park::draw_build_arrow(device, scene, track_arrows(scene), { }, transform);
        }
        if (scene.building.test)
            draw_ghost_mesh(device, scene, scene.building.test->meshes.mesh, transform, light);
    }

    std::vector<save::model::TrackPiece> station_pieces(park::ParkScene& scene, std::size_t ride, const ui::InterfaceAssets& assets)
    {
        std::vector<save::model::TrackPiece> out;
        const auto overlay = ride_track_overlay(scene, ride);
        const auto c = overlay ? load_catalogue(scene.building, *overlay, assets) : std::nullopt;
        if (!c || !scene.rides.rides[ride].track)
            return out;
        for (const save::model::TrackPiece& piece : scene.rides.rides[ride].track->pieces)
        {
            const auto shape = rct3::park::track::shape_named(*c, piece.symbol);
            if (piece.platform || (shape && (shape->record.flags & rct3::park::track::kPieceStation) != 0))
                out.push_back(piece);
        }
        return out;
    }

} // namespace rct3::scene::track
