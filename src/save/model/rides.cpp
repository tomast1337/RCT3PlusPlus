// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/rides.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rct3::save
{

    namespace
    {

        // ---------------------------------------------------------------------------------------------------------
        // The one table of format identifiers this loader keys on: class names and attribute names as they appear
        // in the save's class table.
        namespace id
        {
            // Classes
            constexpr std::string_view kTrackedRide = "TrackedRideInstance";
            constexpr std::string_view kFlatRide = "AnimatedRideInstance";
            constexpr std::string_view kSpecial = "SpecialAttractionInstance";
            constexpr std::string_view kStall = "StallInstance";
            constexpr std::string_view kTrack = "Track";
            constexpr std::string_view kSegment = "TrackSegment";
            constexpr std::string_view kPiece = "TrackPiece";
            constexpr std::string_view kPlatform = "TrackPlatformPiece";
            constexpr std::string_view kTrain = "RideTrainInstance";
            constexpr std::string_view kCar = "RideCarInstance";
            constexpr std::string_view kQueueLine = "QueueLine";
            constexpr std::string_view kFakeQueueLine = "FakeQueueLine";
            constexpr std::string_view kQueueTile = "PathQueue";
            constexpr std::string_view kStationHolder = "RideStations";
            constexpr std::string_view kShopItem = "ShopItemInstance";
            constexpr std::string_view kShopExtra = "ShopExtraInstance";

            // Attraction base
            constexpr std::string_view kKindAsset = "AIDDatabaseEntry";
            constexpr std::string_view kName = "Name";
            constexpr std::string_view kState = "State";
            constexpr std::string_view kFlags = "Flags";
            constexpr std::string_view kBuildTime = "BuildTime";
            constexpr std::string_view kTotalGuests = "TotalGuests";
            constexpr std::string_view kTotalProfit = "TotalProfit";
            constexpr std::string_view kRunningCost = "AssessedRunningCost";
            constexpr std::string_view kPopularity = "Popularity";
            constexpr std::string_view kSatisfaction = "Satisfaction";
            constexpr std::string_view kRepair = "FixableStatus";
            constexpr std::string_view kSinceInspection = "TimeSinceLastInspection";
            constexpr std::string_view kSinceBreakdown = "TimeSinceLastBreakdown";
            constexpr std::string_view kSinceCrash = "TimeSinceLastCrash";
            constexpr std::string_view kPlacement = "Scenery";
            constexpr std::string_view kDirection = "Direction";
            constexpr std::string_view kIntervals = "IntervalData";
            constexpr std::string_view kIntervalGuests = "GuestsPerInterval";
            constexpr std::string_view kIntervalIncome = "IncomePerInterval";
            constexpr std::string_view kIntervalLosses = "LossesPerInterval";
            constexpr std::string_view kThoughts = "ThoughtsAbout";
            constexpr std::string_view kThoughtGuest = "GuestReference";
            constexpr std::string_view kThoughtId = "Thought";
            constexpr std::string_view kThoughtTime = "Time";
            constexpr std::string_view kThoughtVariant = "Variant";
            constexpr std::string_view kColour0 = "COL0";
            constexpr std::string_view kColour1 = "COL1";
            constexpr std::string_view kColour2 = "COL2";

            // Asset symbol and overlay per class
            constexpr std::string_view kTrackedSymbol = "TrackedRideSymbolName";
            constexpr std::string_view kTrackedOverlay = "TrackedRideOverlayName";
            constexpr std::string_view kFlatSymbol = "AnimatedRideSymbolName";
            constexpr std::string_view kFlatOverlay = "AnimatedRideOverlayName";
            constexpr std::string_view kSpecialSymbol = "SpecialAttractionSymbolName";
            constexpr std::string_view kSpecialOverlay = "SpecialAttractionOverlayName";
            constexpr std::string_view kStallSymbol = "StallSymbolName";
            constexpr std::string_view kStallOverlay = "StallOverlayName";

            // Ride
            constexpr std::string_view kOpMode = "OpMode";
            constexpr std::string_view kTicket = "TicketPrice";
            constexpr std::string_view kAssessment = "RideAssessmentResults";
            constexpr std::string_view kAssessValid = "AssessmentIsValid";
            constexpr std::string_view kAssessRatings = "AssessmentRatings";
            constexpr std::string_view kAssessReliability = "AssessmentReliability";
            constexpr std::string_view kAssessCover = "AssessmentUnderCover";
            constexpr std::string_view kAssessValue = "AssessmentValue";
            constexpr std::string_view kStats = "TrackedRideStats";
            constexpr std::string_view kTesting = "TrackedRideTesting";
            constexpr std::string_view kResultsCurrent = "Results";
            constexpr std::string_view kResultsSaved = "SavedTrackedRideTestResults";
            constexpr std::string_view kResultsCombined = "CombinedTrackedRideTestResults";
            constexpr std::string_view kMaxSpeed = "MaxSpeed";
            constexpr std::string_view kAverageSpeed = "AverageSpeed";
            constexpr std::string_view kRideTime = "RideTime";
            constexpr std::string_view kRideLength = "RideLength";
            constexpr std::string_view kMaxPositiveG = "MaxPositiveVerticalG";
            constexpr std::string_view kMaxNegativeG = "MaxNegativeVerticalG";
            constexpr std::string_view kMaxLateralG = "MaxLateralG";
            constexpr std::string_view kAirTime = "AirTime";
            constexpr std::string_view kDrops = "NDrops";
            constexpr std::string_view kHighestDrop = "HighestDropHeight";
            constexpr std::string_view kInversions = "NInversions";
            constexpr std::string_view kLiftHills = "NLiftHills";
            constexpr std::string_view kCorners = "CornerCounts";
            constexpr std::string_view kBankedCorners = "BankedCornerCounts";
            constexpr std::string_view kSlopedCorners = "SlopedCornerCounts";
            constexpr std::string_view kCorner90 = "n90";
            constexpr std::string_view kCorner180 = "n180";
            constexpr std::string_view kCorner270 = "n270";
            constexpr std::string_view kCorner360 = "n360";
            constexpr std::string_view kSections = "TrackSectionCounts";
            constexpr std::string_view kSectionNames[7] = { "nWaterSplash", "nRapids", "nSpinningTunnel", "nWaterFall", "nTurnTable", "nWhirlpool", "nHelices" };
            constexpr std::string_view kValid = "Valid";
            constexpr std::string_view kGraphSpeed = "GraphSpeed";
            constexpr std::string_view kGraphAccel = "GraphAcceleration";
            constexpr std::string_view kGraphAltitude = "GraphAltitude";
            constexpr std::string_view kGraphVertical = "GraphVerticalG";
            constexpr std::string_view kGraphLateral = "GraphLateralG";
            constexpr std::string_view kGraphValues = "values";
            constexpr std::string_view kBreakdownType = "m_breakdownType";
            constexpr std::string_view kLastBreakdownType = "m_lastBreakdownType";
            constexpr std::string_view kBreakdownStation = "m_breakdownStation";
            constexpr std::string_view kBreakdownTrain = "m_breakdownTrain";
            constexpr std::string_view kBreakdownCar = "m_breakdownCar";
            constexpr std::string_view kBreakdownSeat = "m_breakdownSeatIndex";
            constexpr std::string_view kInspectionInterval = "m_inspectionInterval";
            constexpr std::string_view kReliability = "m_reliability";
            constexpr std::string_view kDowntime = "m_downTimeCounters";
            constexpr std::string_view kDowntimePercent = "m_downTimePercent";
            constexpr std::string_view kBlockSectioned = "BlockSectionedMode";
            constexpr std::string_view kSyncTrains = "SyncTrains";
            constexpr std::string_view kLeaveIfEnters = "LeaveIfAnotherTrainEntersStation";
            constexpr std::string_view kMinWait = "MinWaitTime";
            constexpr std::string_view kMaxWait = "MaxWaitTime";
            constexpr std::string_view kWaitForLoad = "WaitForLoad";
            constexpr std::string_view kLaunchSpeed = "LaunchSpeed";
            constexpr std::string_view kLiftSpeed = "LiftChainSpeed";
            constexpr std::string_view kBrakeSpeed = "BrakeSpeed";
            constexpr std::string_view kBlockBrakeOff = "BlockBrakeOffSpeed";
            constexpr std::string_view kFixedSpeed = "FixedSpeed";
            constexpr std::string_view kRaceInProgress = "RaceInProgress";
            constexpr std::string_view kRaceOver = "RaceOver";
            constexpr std::string_view kRaceWinner = "RaceWinnerIndex";
            constexpr std::string_view kRaceSince = "SecondsSinceEndOfRace";
            constexpr std::string_view kCarColours = "CarFlexiColours";
            constexpr std::string_view kFenceColours = "FenceFlexiColours";
            constexpr std::string_view kQueueColours = "QueueFlexiColours";
            constexpr std::string_view kFence = "Fence";
            constexpr std::string_view kOverrideFence = "OverrideFence";
            constexpr std::string_view kPlatformStyle = "PlatformStyle";
            constexpr std::string_view kPlatformType = "PlatformType";
            constexpr std::string_view kTrack_ = "Track";
            constexpr std::string_view kTrains = "Trains";
            constexpr std::string_view kTrainCount = "NTrains";
            constexpr std::string_view kCarsPerTrain = "NCarsPerTrain";
            constexpr std::string_view kTrainSelection = "TrainSelection";
            constexpr std::string_view kSeats = "RideSeats";
            constexpr std::string_view kSeatCar = "TrainCarNo";
            constexpr std::string_view kSeatSeat = "TrainCarSeatNo";
            constexpr std::string_view kSeatTrain = "TrainNo";
            constexpr std::string_view kBlockSections = "BlockSections";
            constexpr std::string_view kSortedTrains = "SortedTrains";
            constexpr std::string_view kCableLift = "CableLiftTrains";
            constexpr std::string_view kThrillLift = "ThrillLiftTrains";
            constexpr std::string_view kReverseCableLift = "ReverseCableLiftTrains";
            constexpr std::string_view kRideState = "RideState";
            constexpr std::string_view kPeopleOnRide = "PeopleOnRide";
            constexpr std::string_view kLoops = "NumLoops";
            constexpr std::string_view kTotalWithdrawn = "TotalWithdrawn";

            // Shows
            constexpr std::string_view kShow = "ShowController";
            constexpr std::string_view kShowRide = "Ride";
            constexpr std::string_view kShowTricks = "AvailableAnimations";
            constexpr std::string_view kShowOrder = "AnimationSequence";
            constexpr std::string_view kTrick = "InventibleAnimation";
            constexpr std::string_view kTrickName = "Name";
            constexpr std::string_view kTrickSeconds = "Duration";
            constexpr std::string_view kTrickHidden = "IsHidden";
            constexpr std::string_view kTrickAvailable = "IsAvailable";
            constexpr std::string_view kTrickInvented = "IsInvented";
            constexpr std::string_view kAverageWithdrawn = "AverageWithdrawn";

            // Stations and queues
            constexpr std::string_view kStations = "RideStations";
            constexpr std::string_view kStationHolderRef = "RideStationsObj";
            constexpr std::string_view kEntrance = "Entrance";
            constexpr std::string_view kExit = "Exit";
            constexpr std::string_view kEntranceTimer = "EntranceTimeRemaining";
            constexpr std::string_view kExitTimer = "ExitTimeRemaining";
            constexpr std::string_view kQueueLineRef = "QueueLine";
            constexpr std::string_view kQueueTime = "m_queueTimeSecs";
            constexpr std::string_view kQueueSize = "QueueSize";

            // Track
            constexpr std::string_view kFirstSegment = "FirstSegment";
            constexpr std::string_view kLastSegment = "LastSegment";
            constexpr std::string_view kTrackRide = "TrackedRideInstance";
            constexpr std::string_view kPrototype = "Prototype";
            constexpr std::string_view kTrackColours = "TrackFlexiColours";
            constexpr std::string_view kNextSegment = "NextSegment";
            constexpr std::string_view kFirstPiece = "FirstPiece";
            constexpr std::string_view kLastPiece = "LastPiece";
            constexpr std::string_view kSegmentTrack = "Track";
            constexpr std::string_view kPieceSymbol = "SYMBOLNAME";
            constexpr std::string_view kPieceAsset = "SIDDatabaseEntry";
            constexpr std::string_view kPieceScenery = "SceneryItem";
            constexpr std::string_view kPlacementField = "SceneryItemDataField";
            constexpr std::string_view kPosX = "POSX";
            constexpr std::string_view kPosZ = "POSZ";
            constexpr std::string_view kHeight = "HEIGHT";
            constexpr std::string_view kHeightAdjust = "HEIGHTADJUST";
            constexpr std::string_view kPieceDirection = "DIRECTION";
            constexpr std::string_view kPieceCorner = "CORNER";
            constexpr std::string_view kReversed = "Reversed";
            constexpr std::string_view kStartDistance = "StartDistance";
            constexpr std::string_view kStartDistanceBack = "StartDistanceBackwardsSpline";
            constexpr std::string_view kPieceColours = "FlexiColourField";
            constexpr std::string_view kPieceBrake = "BrakeSpeed";
            constexpr std::string_view kPieceHolding = "HoldingTime";
            constexpr std::string_view kArmAngles = "RobotArmAngles";
            constexpr std::string_view kArmAnglesList = "Angles";
            constexpr std::string_view kArmAnglesSet = "Set";
            constexpr std::string_view kPieceSpeed = "Speed";
            constexpr std::string_view kNext = "Next";
            constexpr std::string_view kPrev = "Prev";
            constexpr std::string_view kPieceSegment = "Segment";
            constexpr std::string_view kPiecePlatform = "PlatformPiece";
            constexpr std::string_view kPlatformPiece = "TrackPiece";

            // Placement records and asset entries
            constexpr std::string_view kPlacedItem = "SceneryItem";
            constexpr std::string_view kEntrySymbol = "SYMBOLNAME";
            constexpr std::string_view kEntryOverlay = "OVERLAYFILENAME";

            // Trains and cars
            constexpr std::string_view kTrainRide = "TrackedRideInstance";
            constexpr std::string_view kCars = "Cars";
            constexpr std::string_view kWhichTrain = "WhichTrain";
            constexpr std::string_view kStateTime = "StateTime";
            constexpr std::string_view kFrontPiece = "FrontTrackPiece";
            constexpr std::string_view kRearPiece = "RearTrackPiece";
            constexpr std::string_view kDistance = "Distance";
            constexpr std::string_view kSpeed = "Speed";
            constexpr std::string_view kAccel = "Accel";
            constexpr std::string_view kStation = "Station";
            constexpr std::string_view kStartStation = "StartStation";
            constexpr std::string_view kOnStation = "OnWhichStation";
            constexpr std::string_view kCircuits = "CicuitCount";
            constexpr std::string_view kShuttle = "ShuttleDirection";
            constexpr std::string_view kStaticFriction = "StaticFrictionCoeff";
            constexpr std::string_view kDynamicFriction = "DynamicFrictionCoeff";
            constexpr std::string_view kMass = "Mass";
            constexpr std::string_view kLength = "Length";
            constexpr std::string_view kTrainSymbol = "RideTrainSymbolName";
            constexpr std::string_view kTrainOverlay = "RideTrainOverlayName";
            constexpr std::string_view kCarTrain = "RideTrainInstance";
            constexpr std::string_view kWhichCar = "WhichCar";
            constexpr std::string_view kCarCount = "NCars";
            constexpr std::string_view kCarSlot = "WhichRideTrainCar";
            constexpr std::string_view kPeeps = "NumPeepsInCar";
            constexpr std::string_view kCarStats = "Stats";
            constexpr std::string_view kStatPos = "Pos";
            constexpr std::string_view kStatPosValid = "PosValid";
            constexpr std::string_view kStatOrient = "Orient";
            constexpr std::string_view kStatPiece = "TrackPiece";
            constexpr std::string_view kFrontWheel = "FrontWheelDistance";
            constexpr std::string_view kRearWheel = "RearWheelDistance";

            // Stalls
            constexpr std::string_view kShopItems = "ShopItems";
            constexpr std::string_view kShopExtras = "ShopExtras";
            constexpr std::string_view kItemAsset = "CIDEntry";
            constexpr std::string_view kItemKindSymbol = "SymbolName"; // On the carried-item asset entry
            constexpr std::string_view kPrice = "Price";
            constexpr std::string_view kSold = "TotalSold";
            constexpr std::string_view kItemStall = "Stall";
            constexpr std::string_view kSaleItem = "SaleItem";
            constexpr std::string_view kItemColours[3] = { "FlexiColour0", "FlexiColour1", "FlexiColour2" };
            constexpr std::string_view kExtraSetting = "Setting";
        } // namespace id

        // ---------------------------------------------------------------------------------------------------------
        // Null-safe typed access to a decoded value.

        constexpr float kFloatLimit = 1.0e8f; // larger magnitudes are uninitialised fill, not data

        class View
        {
        public:
            View() = default;
            explicit View(std::optional<const Value&> v)
                : v_(v)
            {
            }

            [[nodiscard]] View operator[](std::string_view name) const
            {
                return View(v_ ? v_->field(name) : std::nullopt);
            }
            [[nodiscard]] bool present() const
            {
                return v_.has_value();
            }

            [[nodiscard]] std::int64_t integer(std::int64_t def = 0) const
            {
                if (!v_)
                    return def;
                switch (v_->kind())
                {
                    case ValueKind::Integer:
                        return v_->as_integer();
                    case ValueKind::Bool:
                        return v_->as_bool() ? 1 : 0;
                    case ValueKind::Float:
                    {
                        const float f = v_->as_float();
                        if (!std::isfinite(f) || std::fabs(f) > kFloatLimit)
                            return def;
                        return static_cast<std::int64_t>(f);
                    }
                    default:
                        return def;
                }
            }
            [[nodiscard]] std::int32_t i32(std::int32_t def = 0) const
            {
                return static_cast<std::int32_t>(integer(def));
            }
            [[nodiscard]] float number(float def = 0) const
            {
                if (!v_)
                    return def;
                float f = def;
                if (v_->kind() == ValueKind::Float)
                    f = v_->as_float();
                else if (v_->kind() == ValueKind::Integer)
                    f = static_cast<float>(v_->as_integer());
                else
                    return def;
                if (!std::isfinite(f) || std::fabs(f) > kFloatLimit)
                    return def;
                return f;
            }
            [[nodiscard]] bool boolean(bool def = false) const
            {
                if (!v_ || (v_->kind() != ValueKind::Bool && v_->kind() != ValueKind::Integer))
                    return def;
                return v_->kind() == ValueKind::Bool ? v_->as_bool() : v_->as_integer() != 0;
            }
            [[nodiscard]] std::string text() const
            {
                if (!v_ || v_->kind() != ValueKind::String)
                    return { };
                return std::string(v_->text());
            }
            [[nodiscard]] std::uint64_t ref() const
            {
                return (v_ && v_->kind() == ValueKind::Reference) ? v_->as_reference() : 0;
            }
            [[nodiscard]] std::span<const Value> items() const
            {
                if (!v_ || v_->kind() != ValueKind::List)
                    return { };
                return v_->items();
            }
            [[nodiscard]] std::span<const float> floats() const
            {
                if (!v_)
                    return { };
                switch (v_->kind())
                {
                    case ValueKind::Vector3:
                    case ValueKind::Orientation:
                    case ValueKind::Matrix44:
                    case ValueKind::FloatArray:
                        return v_->floats();
                    default:
                        return { };
                }
            }

        private:
            std::optional<const Value&> v_;
        };

        [[nodiscard]] RideColours colours_of(View v, std::string_view a, std::string_view b, std::string_view c)
        {
            return { static_cast<std::uint8_t>(v[a].integer()), static_cast<std::uint8_t>(v[b].integer()), static_cast<std::uint8_t>(v[c].integer()) };
        }
        [[nodiscard]] RideColours colours_of(View v)
        {
            return colours_of(v, id::kColour0, id::kColour1, id::kColour2);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Load context

        enum class Role : std::uint8_t
        {
            Other,
            TrackedRide,
            FlatRide,
            Special,
            Stall,
            Track,
            Segment,
            Piece,
            Platform,
            Train,
            Car,
            QueueLine,
            QueueTile,
            StationHolder,
            ShopItem,
            ShopExtra,
        };

        [[nodiscard]] Role role_of(std::string_view name)
        {
            struct Entry
            {
                std::string_view name;
                Role role;
            };
            static constexpr Entry kTable[] = {
                { id::kTrackedRide, Role::TrackedRide }, { id::kFlatRide, Role::FlatRide },           { id::kSpecial, Role::Special },   { id::kStall, Role::Stall },         { id::kTrack, Role::Track }, { id::kSegment, Role::Segment }, { id::kPiece, Role::Piece }, { id::kPlatform, Role::Platform }, { id::kTrain, Role::Train }, { id::kCar, Role::Car }, { id::kQueueLine, Role::QueueLine }, { id::kFakeQueueLine, Role::QueueLine },
                { id::kQueueTile, Role::QueueTile },     { id::kStationHolder, Role::StationHolder }, { id::kShopItem, Role::ShopItem }, { id::kShopExtra, Role::ShopExtra },
            };
            for (const auto& e : kTable)
            {
                if (e.name == name)
                    return e.role;
            }
            return Role::Other;
        }

        struct PieceRecord
        {
            TrackPiece piece;
            ObjectId next = 0;
            ObjectId prev = 0;
            ObjectId segment_ref = 0;
            ObjectId platform_ref = 0;
        };

        struct PlatformRecord
        {
            TrackPlatform platform;
            ObjectId piece_ref = 0;
        };

        class Loader
        {
        public:
            Loader(const SaveFile& file, const ObjectIndex& index)
                : file_(file)
                , index_(index)
            {
                roles_.reserve(file.classes.size());
                for (const auto& c : file.classes)
                    roles_.push_back(role_of(c.name));
            }

            [[nodiscard]] std::expected<RideModel, Error> run()
            {
                scan();
                if (failure_)
                    return std::unexpected(*failure_);
                for (const auto& o : file_.objects)
                {
                    const Role r = roles_[o.class_index];
                    if (r == Role::TrackedRide || r == Role::FlatRide || r == Role::Special)
                        load_ride(o, r);
                    else if (r == Role::Stall)
                        load_stall(o);
                    if (failure_)
                        return std::unexpected(*failure_);
                }
                model_.orphan_pieces = pieces_.size() > reached_.size() ? pieces_.size() - reached_.size() : 0;
                return std::move(model_);
            }

        private:
            const SaveFile& file_;
            const ObjectIndex& index_;
            std::vector<Role> roles_;
            RideModel model_;
            std::optional<Error> failure_;

            std::unordered_map<ObjectId, PieceRecord> pieces_;
            std::unordered_map<ObjectId, PlatformRecord> platforms_;
            std::unordered_map<ObjectId, std::int32_t> queue_tiles_;
            std::unordered_map<ObjectId, std::size_t> segment_owned_;
            std::unordered_set<ObjectId> reached_; // pieces reached by some track
            std::unordered_set<ObjectId> taken_;   // trains, cars, items and extras already attached

            [[nodiscard]] ObjectId live(View v) const
            {
                const ObjectId r = v.ref();
                return (r != 0 && index_.find(r).has_value()) ? r : 0;
            }

            /// Decodes the object with this id when it exists and has the wanted role.
            [[nodiscard]] std::optional<DecodedObject> fetch(ObjectId oid, Role want)
            {
                if (oid == 0)
                    return std::nullopt;
                std::optional<const Object&> o = index_.find(oid);
                if (!o || roles_[o->class_index] != want)
                    return std::nullopt;
                return decode(*o);
            }

            [[nodiscard]] std::optional<DecodedObject> decode(const Object& o)
            {
                auto d = decode_object(file_, o);
                if (!d)
                {
                    failure_ = d.error();
                    return std::nullopt;
                }
                return std::move(*d);
            }

            // Pre-pass: pieces, platforms and queue tiles are looked up by id many times.
            void scan()
            {
                for (const auto& o : file_.objects)
                {
                    const Role r = roles_[o.class_index];
                    if (r != Role::Piece && r != Role::Platform && r != Role::QueueTile)
                        continue;
                    auto d = decode(o);
                    if (!d)
                        return;
                    const View f(d->fields);
                    if (r == Role::Piece)
                    {
                        const View at = f[id::kPlacementField];
                        PieceRecord rec {
                            .piece = {
                                .id = d->id,
                                .symbol = f[id::kPieceSymbol].text(),
                                .asset = live(f[id::kPieceAsset]),
                                .scenery = live(f[id::kPieceScenery]),
                                .tile_x = at[id::kPosX].i32(),
                                .tile_z = at[id::kPosZ].i32(),
                                .height = at[id::kHeight].i32(),
                                .height_adjust = at[id::kHeightAdjust].number(),
                                .direction = static_cast<std::uint8_t>(at[id::kPieceDirection].integer()),
                                .corner = static_cast<std::uint8_t>(at[id::kPieceCorner].integer()),
                                .reversed = f[id::kReversed].boolean(),
                                .start_distance = f[id::kStartDistance].number(),
                                .reverse_start_distance = f[id::kStartDistanceBack].number(),
                                .colours = colours_of(f[id::kPieceColours]),
                                .brake_speed = f[id::kPieceBrake].i32(-1),
                                .holding_time = f[id::kPieceHolding].i32(0),
                                .speed_class = f[id::kPieceSpeed].i32(-1),
                            },
                            .next = f[id::kNext].ref(),
                            .prev = f[id::kPrev].ref(),
                            .segment_ref = f[id::kPieceSegment].ref(),
                            .platform_ref = f[id::kPiecePlatform].ref(),
                        };
                        if (const View arm = f[id::kArmAngles]; arm[id::kArmAnglesSet].boolean())
                        {
                            const auto angles = arm[id::kArmAnglesList].items();
                            std::array<float, 4> held { };
                            for (std::size_t j = 0; j < held.size() && j < angles.size(); ++j)
                                held[j] = View(angles[j]).number();
                            rec.piece.arm_angles = held;
                        }
                        if (rec.segment_ref != 0)
                            ++segment_owned_[rec.segment_ref];
                        pieces_.emplace(d->id, std::move(rec));
                    }
                    else if (r == Role::Platform)
                    {
                        const View at = f[id::kPlacementField];
                        platforms_.emplace(d->id, PlatformRecord {
                            .platform = {
                                .id = d->id,
                                .tile_x = at[id::kPosX].i32(),
                                .tile_z = at[id::kPosZ].i32(),
                                .height = at[id::kHeight].i32(),
                                .direction = static_cast<std::uint8_t>(at[id::kPieceDirection].integer()),
                                .corner = static_cast<std::uint8_t>(at[id::kPieceCorner].integer()),
                                .flags = static_cast<std::uint32_t>(f[id::kFlags].integer()),
                                .colours = colours_of(f[id::kPieceColours]),
                            },
                            .piece_ref = f[id::kPlatformPiece].ref(),
                        });
                    }
                    else
                    {
                        ++queue_tiles_[f[id::kQueueLineRef].ref()];
                    }
                }
            }

            // ---- attractions ------------------------------------------------------------------------------------

            /// Where the placement record `oid` puts its object; nothing when it is none or carries no position.
            std::optional<PlacementSpot> spot_of(ObjectId oid)
            {
                std::optional<const Object&> o = oid != 0 ? index_.find(oid) : std::nullopt;
                if (!o)
                    return std::nullopt;
                const auto d = decode(*o);
                if (!d)
                    return std::nullopt;
                const View f(d->fields);
                const View at = f[id::kPlacementField];
                if (!at.present())
                    return std::nullopt;
                return PlacementSpot {
                    .object = live(f[id::kPlacedItem]),
                    .col = at[id::kPosX].i32(),
                    .row = at[id::kPosZ].i32(),
                    .height = at[id::kHeight].i32(),
                    .direction = static_cast<std::uint8_t>(at[id::kPieceDirection].integer() & 3),
                };
            }

            /// The symbol and overlay of the asset entry `oid`, both empty when it is none.
            std::pair<std::string, std::string> asset_of(ObjectId oid)
            {
                std::optional<const Object&> o = oid != 0 ? index_.find(oid) : std::nullopt;
                if (!o)
                    return { };
                const auto d = decode(*o);
                if (!d)
                    return { };
                const View f(d->fields);
                return { f[id::kEntrySymbol].text(), f[id::kEntryOverlay].text() };
            }

            AttractionCommon read_common(const DecodedObject& d, std::string_view symbol_key, std::string_view overlay_key)
            {
                const View f(d.fields);
                const ObjectId placement = live(f[id::kPlacement]);
                return AttractionCommon {
                    .id = d.id,
                    .kind_asset = live(f[id::kKindAsset]),
                    .symbol = f[symbol_key].text(),
                    .overlay_path = f[overlay_key].text(),
                    .name = f[id::kName].text(),
                    .state = static_cast<AttractionState>(f[id::kState].i32()),
                    .flags = static_cast<std::uint32_t>(f[id::kFlags].integer()),
                    .build_time = f[id::kBuildTime].number(),
                    .total_guests = f[id::kTotalGuests].integer(),
                    .total_profit = f[id::kTotalProfit].integer(),
                    .running_cost = f[id::kRunningCost].integer(),
                    .popularity = f[id::kPopularity].number(),
                    .satisfaction = f[id::kSatisfaction].number(),
                    .repair_status = static_cast<RepairStatus>(f[id::kRepair].i32()),
                    .time_since_inspection = f[id::kSinceInspection].number(-1),
                    .time_since_breakdown = f[id::kSinceBreakdown].number(-1),
                    .time_since_crash = f[id::kSinceCrash].number(-1),
                    .placement = placement,
                    .spot = spot_of(placement),
                    .facing = static_cast<std::uint8_t>(f[id::kDirection].integer()),
                };
            }

            static std::vector<IntervalStats> read_intervals(View f)
            {
                std::vector<IntervalStats> out;
                for (const auto& item : f[id::kIntervals].items())
                {
                    const View v(item);
                    out.push_back({ v[id::kIntervalGuests].integer(), v[id::kIntervalIncome].integer(), v[id::kIntervalLosses].integer() });
                }
                return out;
            }

            std::vector<ThoughtEntry> read_thoughts(View f)
            {
                std::vector<ThoughtEntry> out;
                for (const auto& item : f[id::kThoughts].items())
                {
                    const View v(item);
                    const ObjectId guest = live(v[id::kThoughtGuest]);
                    if (guest == 0)
                        continue; // empty or stale slot
                    out.push_back({ guest, v[id::kThoughtId].i32(), v[id::kThoughtTime].number(), v[id::kThoughtVariant].integer() });
                }
                return out;
            }

            static TestResults read_results(View r)
            {
                const auto corners = [](View c) { return std::array<std::int32_t, 4> { c[id::kCorner90].i32(), c[id::kCorner180].i32(), c[id::kCorner270].i32(), c[id::kCorner360].i32() }; };
                TestResults t {
                    .max_speed = r[id::kMaxSpeed].number(),
                    .average_speed = r[id::kAverageSpeed].number(),
                    .ride_time = r[id::kRideTime].number(),
                    .ride_length = r[id::kRideLength].number(),
                    .max_positive_vertical = r[id::kMaxPositiveG].number(),
                    .max_negative_vertical = r[id::kMaxNegativeG].number(),
                    .max_lateral = r[id::kMaxLateralG].number(),
                    .air_time = r[id::kAirTime].number(),
                    .drop_count = r[id::kDrops].i32(),
                    .highest_drop = r[id::kHighestDrop].number(),
                    .inversion_count = r[id::kInversions].i32(),
                    .lift_hill_count = r[id::kLiftHills].i32(),
                    .corners_flat = corners(r[id::kCorners]),
                    .corners_banked = corners(r[id::kBankedCorners]),
                    .corners_sloped = corners(r[id::kSlopedCorners]),
                };
                for (std::size_t i = 0; i < 7; ++i)
                    t.special_sections[i] = r[id::kSections][id::kSectionNames[i]].i32();
                t.valid = (r[id::kValid].integer() & 1) != 0; // upper bytes are uninitialised memory
                return t;
            }

            static std::vector<float> read_graph(View g)
            {
                const auto fl = g[id::kGraphValues].floats();
                auto out = fl | std::ranges::to<std::vector<float>>();
                for (auto& x : out)
                {
                    if (!std::isfinite(x) || std::fabs(x) > kFloatLimit)
                        x = 0;
                }
                return out;
            }

            std::vector<ObjectId> live_list(View list)
            {
                std::vector<ObjectId> out;
                for (const auto& item : list.items())
                    out.push_back(live(View(item)));
                return out;
            }

            void read_stations(Ride& ride, View f)
            {
                View list = f[id::kStations];
                std::optional<DecodedObject> holder;
                if (const ObjectId h = f[id::kStationHolderRef].ref(); h != 0)
                {
                    holder = fetch(h, Role::StationHolder);
                    if (holder)
                        list = View(holder->fields)[id::kStations];
                }
                for (const auto& item : list.items())
                {
                    const View v(item);
                    const ObjectId entrance = live(v[id::kEntrance]);
                    const ObjectId exit = live(v[id::kExit]);
                    Station s {
                        .entrance = entrance,
                        .exit = exit,
                        .entrance_spot = spot_of(entrance),
                        .exit_spot = spot_of(exit),
                        .entrance_timer = v[id::kEntranceTimer].number(),
                        .exit_timer = v[id::kExitTimer].number(),
                        .queue_time = v[id::kQueueTime].number(),
                    };
                    if (auto q = fetch(v[id::kQueueLineRef].ref(), Role::QueueLine))
                    {
                        s.queue_line = q->id;
                        s.queue_size = View(q->fields)[id::kQueueSize].i32();
                        if (const auto it = queue_tiles_.find(q->id); it != queue_tiles_.end())
                            s.queue_tiles = it->second;
                        if (s.queue_size != s.queue_tiles)
                            ++model_.diagnostics.queue_size_mismatch;
                    }
                    ride.stations.push_back(s);
                }
            }

            void read_ride_common(Ride& ride, const DecodedObject& d)
            {
                const View f(d.fields);
                ride.mode = static_cast<OperatingMode>(f[id::kOpMode].i32());
                ride.ticket_price = f[id::kTicket].integer();
                ride.intervals = read_intervals(f);
                ride.thoughts = read_thoughts(f);

                if (const View a = f[id::kAssessment]; a[id::kAssessValid].boolean())
                {
                    const auto r = a[id::kAssessRatings].floats();
                    const auto at = [&](std::size_t i) {
                        const float x = i < r.size() ? r[i] : 0;
                        return (std::isfinite(x) && std::fabs(x) <= kFloatLimit) ? x : 0.0f;
                    };
                    ride.assessment = Assessment {
                        .valid = true,
                        .excitement = at(0),
                        .intensity = at(1),
                        .nausea = at(2),
                        .reliability_base = a[id::kAssessReliability].number(),
                        .under_cover = a[id::kAssessCover].number(),
                        .value = a[id::kAssessValue].number(),
                    };
                }

                const View testing = f[id::kStats][id::kTesting];
                if (testing[id::kResultsCurrent].present())
                    ride.stats.current_lap = read_results(testing[id::kResultsCurrent]);
                if (testing[id::kResultsSaved].present())
                    ride.stats.last_lap = read_results(testing[id::kResultsSaved]);
                if (testing[id::kResultsCombined].present())
                    ride.stats.combined = read_results(testing[id::kResultsCombined]);

                ride.graphs = RideGraphs {
                    .speed = read_graph(f[id::kGraphSpeed]),
                    .acceleration = read_graph(f[id::kGraphAccel]),
                    .altitude = read_graph(f[id::kGraphAltitude]),
                    .vertical_g = read_graph(f[id::kGraphVertical]),
                    .lateral_g = read_graph(f[id::kGraphLateral]),
                };

                ride.breakdown = Breakdown {
                    .current = static_cast<BreakdownKind>(f[id::kBreakdownType].i32()),
                    .last = static_cast<BreakdownKind>(f[id::kLastBreakdownType].i32()),
                    .station = f[id::kBreakdownStation].i32(-1),
                    .train = f[id::kBreakdownTrain].i32(-1),
                    .car = f[id::kBreakdownCar].i32(-1),
                    .seat = f[id::kBreakdownSeat].i32(-1),
                    .inspection_interval = f[id::kInspectionInterval].i32(),
                    .reliability = f[id::kReliability].number(100),
                };
                Breakdown& b = ride.breakdown;
                if (f[id::kDowntimePercent].present())
                    b.saved_downtime_percent = std::clamp(f[id::kDowntimePercent].number(), 0.0f, 100.0f);
                std::size_t i = 0;
                for (const auto& item : f[id::kDowntime].items())
                {
                    if (i >= b.downtime_windows.size())
                        break;
                    const std::int64_t w = View(item).integer();
                    b.downtime_windows[i++] = static_cast<std::uint8_t>(w < 0 ? 0 : (w > 20 ? 20 : w));
                }

                ride.options = OperatingOptions {
                    .block_sectioned = f[id::kBlockSectioned].boolean(),
                    .sync_trains = f[id::kSyncTrains].boolean(),
                    .leave_if_other_train_enters_station = f[id::kLeaveIfEnters].boolean(),
                    .min_wait = f[id::kMinWait].i32(-1),
                    .max_wait = f[id::kMaxWait].i32(-1),
                    .wait_for_load = f[id::kWaitForLoad].i32(4),
                    .launch_speed = f[id::kLaunchSpeed].number(),
                    .lift_speed = f[id::kLiftSpeed].number(),
                    .brake_speed = f[id::kBrakeSpeed].number(),
                    .block_brake_off_speed = f[id::kBlockBrakeOff].number(),
                    .fixed_speed = f[id::kFixedSpeed].number(-1),
                };

                ride.race = RaceState {
                    .in_progress = f[id::kRaceInProgress].boolean(),
                    .over = f[id::kRaceOver].boolean(),
                    .winner = f[id::kRaceWinner].i32(-1),
                    .seconds_since_end = f[id::kRaceSince].number(),
                };

                ride.car_colours = colours_of(f[id::kCarColours]);
                ride.fence_colours = colours_of(f[id::kFenceColours]);
                ride.queue_colours = colours_of(f[id::kQueueColours]);
                ride.fence = live(f[id::kFence]);
                ride.override_fence = live(f[id::kOverrideFence]);
                std::tie(ride.fence_symbol, ride.fence_overlay) = asset_of(ride.fence);
                std::tie(ride.override_fence_symbol, ride.override_fence_overlay) = asset_of(ride.override_fence);
                ride.platform_style = f[id::kPlatformStyle].text();
                ride.platform_type = f[id::kPlatformType].text();
                read_stations(ride, f);
            }

            void load_ride(const Object& obj, Role role)
            {
                auto d = decode(obj);
                if (!d)
                    return;
                const View f(d->fields);
                Ride ride;
                switch (role)
                {
                    case Role::TrackedRide:
                        ride.kind = RideKind::Tracked;
                        ride.base = read_common(*d, id::kTrackedSymbol, id::kTrackedOverlay);
                        break;
                    case Role::FlatRide:
                        ride.kind = RideKind::Flat;
                        ride.base = read_common(*d, id::kFlatSymbol, id::kFlatOverlay);
                        break;
                    default:
                        ride.kind = RideKind::Special;
                        ride.base = read_common(*d, id::kSpecialSymbol, id::kSpecialOverlay);
                        break;
                }
                read_ride_common(ride, *d);

                if (role == Role::TrackedRide)
                {
                    ride.train_count = f[id::kTrainCount].i32(-1);
                    ride.cars_per_train = f[id::kCarsPerTrain].i32(-1);
                    ride.train_selection = f[id::kTrainSelection].i32();
                    for (const auto& item : f[id::kSeats].items())
                    {
                        const View v(item);
                        ride.seats.push_back({ v[id::kSeatCar].i32(), v[id::kSeatSeat].i32(), v[id::kSeatTrain].i32() });
                    }
                    ride.block_sections = live_list(f[id::kBlockSections]);
                    for (const auto& item : f[id::kSortedTrains].items())
                        ride.sorted_trains.push_back(View(item).i32());
                    ride.cable_lift_trains = live_list(f[id::kCableLift]);
                    ride.thrill_lift_trains = live_list(f[id::kThrillLift]);
                    ride.reverse_cable_lift_trains = live_list(f[id::kReverseCableLift]);

                    ride.track_id = live(f[id::kTrack_]);
                    if (auto t = fetch(ride.track_id, Role::Track))
                    {
                        ride.track = build_track(*t);
                        if (ride.track->ride != ride.base.id)
                            ++model_.diagnostics.track_back_link_mismatch;
                    }
                    else
                    {
                        ++model_.diagnostics.unresolved_tracks;
                    }
                    load_trains(ride, f);
                }
                else if (role == Role::FlatRide)
                {
                    ride.ride_state = f[id::kRideState].i32();
                    ride.people_on_ride = f[id::kPeopleOnRide].i32();
                    ride.loop_count = f[id::kLoops].i32();
                }
                else
                {
                    ride.total_withdrawn = f[id::kTotalWithdrawn].integer();
                    ride.average_withdrawn = f[id::kAverageWithdrawn].integer();
                }
                model_.rides.push_back(std::move(ride));
            }

            // ---- track ------------------------------------------------------------------------------------------

            Track build_track(const DecodedObject& d)
            {
                const View f(d.fields);
                Track t {
                    .id = d.id,
                    .ride = live(f[id::kTrackRide]),
                    .facing = static_cast<std::uint8_t>(f[id::kDirection].integer()),
                    .prototype = f[id::kPrototype].boolean(),
                    .colours = colours_of(f[id::kTrackColours]),
                };

                const ObjectId last_segment = f[id::kLastSegment].ref();
                ObjectId seg_id = f[id::kFirstSegment].ref();
                std::unordered_set<ObjectId> seen_segments;
                bool reached_last_segment = false;
                ObjectId last_piece_id = 0, first_piece_id = 0;
                while (seg_id != 0)
                {
                    if (!seen_segments.insert(seg_id).second)
                    {
                        t.truncated = true;
                        break;
                    }
                    auto s = fetch(seg_id, Role::Segment);
                    if (!s)
                    {
                        t.truncated = true;
                        break;
                    }
                    const View sf(s->fields);
                    TrackSegment seg {
                        .id = seg_id,
                        .first_piece = t.pieces.size(),
                    };
                    if (const auto it = segment_owned_.find(seg_id); it != segment_owned_.end())
                        seg.owned_pieces = it->second;
                    const std::uint32_t seg_index = static_cast<std::uint32_t>(t.segments.size());
                    const ObjectId seg_last = sf[id::kLastPiece].ref();
                    ObjectId p = sf[id::kFirstPiece].ref();
                    while (p != 0)
                    {
                        const auto it = pieces_.find(p);
                        if (it == pieces_.end() || reached_.contains(p))
                        {
                            t.truncated = true;
                            break;
                        }
                        reached_.insert(p);
                        TrackPiece piece = it->second.piece;
                        piece.segment = seg_index;
                        if (it->second.segment_ref != seg_id)
                            ++model_.diagnostics.piece_segment_mismatch;
                        if (const auto pl = platforms_.find(it->second.platform_ref); pl != platforms_.end())
                        {
                            piece.platform = pl->second.platform;
                            if (pl->second.piece_ref != p)
                                ++model_.diagnostics.platform_back_link_mismatch;
                        }
                        if (t.pieces.empty())
                            first_piece_id = p;
                        last_piece_id = p;
                        t.pieces.push_back(std::move(piece));
                        if (p == seg_last)
                            break;
                        p = it->second.next;
                        if (p == 0)
                            t.truncated = true;
                    }
                    seg.piece_count = t.pieces.size() - seg.first_piece;
                    if (seg.piece_count > 0 && !t.truncated)
                    {
                        const PieceRecord& sfirst = pieces_.at(t.pieces[seg.first_piece].id);
                        const ObjectId slast = t.pieces.back().id;
                        seg.closed = pieces_.at(slast).next == t.pieces[seg.first_piece].id && sfirst.prev == slast;
                    }
                    if (seg.piece_count != seg.owned_pieces)
                        ++model_.diagnostics.segment_piece_mismatch;
                    t.segments.push_back(seg);
                    if (t.truncated)
                        break;
                    if (seg_id == last_segment)
                    {
                        reached_last_segment = true;
                        break;
                    }
                    seg_id = sf[id::kNextSegment].ref();
                }
                if (!reached_last_segment && last_segment != 0)
                    t.truncated = true;

                if (!t.pieces.empty())
                {
                    const PieceRecord& first = pieces_.at(first_piece_id);
                    const PieceRecord& last = pieces_.at(last_piece_id);
                    bool all_closed = !t.segments.empty();
                    for (const auto& sg : t.segments)
                        all_closed = all_closed && sg.closed;
                    t.circuit = !t.truncated && ((last.next == first_piece_id && first.prev == last_piece_id) || all_closed);
                    if (!t.circuit && !t.truncated && (pieces_.contains(last.next) || pieces_.contains(first.prev)))
                        ++model_.diagnostics.open_ends_with_live_link;
                }
                if (t.truncated)
                    ++model_.diagnostics.truncated_tracks;
                return t;
            }

            // ---- trains and cars --------------------------------------------------------------------------------

            void load_trains(Ride& ride, View f)
            {
                std::int32_t position = 0;
                for (const auto& item : f[id::kTrains].items())
                {
                    const std::int32_t at = position++;
                    const ObjectId tid = View(item).ref();
                    if (tid == 0)
                        continue;
                    if (!taken_.insert(tid).second)
                    {
                        ++model_.diagnostics.duplicate_refs;
                        continue;
                    }
                    auto d = fetch(tid, Role::Train);
                    if (!d)
                    {
                        ++model_.diagnostics.unresolved_trains;
                        continue;
                    }
                    const View tf(d->fields);
                    Train t {
                        .id = d->id,
                        .ride = tf[id::kTrainRide].ref(),
                        .index = tf[id::kWhichTrain].i32(),
                        .state = static_cast<TrainState>(tf[id::kState].i32()),
                        .state_time = tf[id::kStateTime].number(),
                        .front_piece = live(tf[id::kFrontPiece]),
                        .rear_piece = live(tf[id::kRearPiece]),
                        .distance = tf[id::kDistance].number(),
                        .speed = tf[id::kSpeed].number(),
                        .acceleration = tf[id::kAccel].number(),
                        .station = tf[id::kStation].i32(-1),
                        .start_station = tf[id::kStartStation].i32(-1),
                        .current_station = tf[id::kOnStation].i32(-1),
                        .lap_count = tf[id::kCircuits].i32(),
                        .shuttle_direction = tf[id::kShuttle].i32(),
                        .static_friction = tf[id::kStaticFriction].number(),
                        .dynamic_friction = tf[id::kDynamicFriction].number(),
                        .mass = tf[id::kMass].number(),
                        .length = tf[id::kLength].number(),
                        .symbol = tf[id::kTrainSymbol].text(),
                        .overlay_path = tf[id::kTrainOverlay].text(),
                    };
                    if (t.ride != ride.base.id)
                        ++model_.diagnostics.train_back_link_mismatch;
                    if (t.index != at)
                        ++model_.diagnostics.train_index_mismatch;

                    const auto car_list = tf[id::kCars].items();
                    std::int32_t cpos = 0;
                    for (const auto& citem : car_list)
                    {
                        const std::int32_t cat = cpos++;
                        const ObjectId cid = View(citem).ref();
                        if (cid == 0)
                            continue;
                        if (!taken_.insert(cid).second)
                        {
                            ++model_.diagnostics.duplicate_refs;
                            continue;
                        }
                        auto c = fetch(cid, Role::Car);
                        if (!c)
                        {
                            ++model_.diagnostics.unresolved_cars;
                            continue;
                        }
                        const View cf(c->fields);
                        const View st = cf[id::kCarStats];
                        const auto pos = st[id::kStatPos].floats();
                        Car car {
                            .id = c->id,
                            .index = cf[id::kWhichCar].i32(),
                            .car_count = cf[id::kCarCount].i32(),
                            .design_slot = cf[id::kCarSlot].i32(),
                            .state = static_cast<CarState>(cf[id::kState].i32()),
                            .passengers = cf[id::kPeeps].i32(),
                            .distance = st[id::kDistance].number(),
                            .speed = st[id::kSpeed].number(),
                            .length = st[id::kLength].number(),
                            .mass = st[id::kMass].number(),
                            .reversed = st[id::kReversed].boolean(),
                            .front_wheel_distance = cf[id::kFrontWheel].number(),
                            .rear_wheel_distance = cf[id::kRearWheel].number(),
                            .position_valid = st[id::kStatPosValid].boolean() && pos.size() == 3,
                            .front_piece = live(st[id::kStatPiece]),
                            .rear_piece = live(st[id::kRearPiece]),
                        };
                        if (car.position_valid)
                        {
                            for (std::size_t i = 0; i < 3; ++i)
                            {
                                car.position[i] = (std::isfinite(pos[i]) && std::fabs(pos[i]) <= kFloatLimit) ? pos[i] : 0.0f;
                            }
                        }
                        if (const auto turn = st[id::kStatOrient].floats(); turn.size() == 3)
                        {
                            for (std::size_t i = 0; i < 3; ++i)
                                car.orientation[i] = (std::isfinite(turn[i]) && std::fabs(turn[i]) <= kFloatLimit) ? turn[i] : 0.0f;
                        }
                        if (cf[id::kCarTrain].ref() != t.id)
                            ++model_.diagnostics.car_back_link_mismatch;
                        if (car.index != cat)
                            ++model_.diagnostics.car_index_mismatch;
                        if (static_cast<std::size_t>(car.car_count) != car_list.size())
                            ++model_.diagnostics.car_count_mismatch;
                        t.cars.push_back(car);
                    }
                    ride.trains.push_back(std::move(t));
                }
            }

            // ---- stalls -----------------------------------------------------------------------------------------

            void load_stall(const Object& obj)
            {
                auto d = decode(obj);
                if (!d)
                    return;
                const View f(d->fields);
                Stall s {
                    .base = read_common(*d, id::kStallSymbol, id::kStallOverlay),
                    .intervals = read_intervals(f),
                    .thoughts = read_thoughts(f),
                };
                for (const auto& item : f[id::kShopItems].items())
                {
                    const ObjectId iid = View(item).ref();
                    if (iid == 0)
                        continue;
                    if (!taken_.insert(iid).second)
                    {
                        ++model_.diagnostics.duplicate_refs;
                        continue;
                    }
                    auto it = fetch(iid, Role::ShopItem);
                    if (!it)
                    {
                        ++model_.diagnostics.unresolved_items;
                        continue;
                    }
                    const View v(it->fields);
                    StallItem si {
                        .id = it->id,
                        .item_asset = live(v[id::kItemAsset]),
                        .price = v[id::kPrice].integer(),
                        .total_sold = v[id::kSold].integer(),
                        .name = v[id::kName].text(),
                        .stall = v[id::kItemStall].ref(),
                        .sale_item = live(v[id::kSaleItem]),
                        .colours = { static_cast<std::uint8_t>(v[id::kItemColours[0]].integer()), static_cast<std::uint8_t>(v[id::kItemColours[1]].integer()), static_cast<std::uint8_t>(v[id::kItemColours[2]].integer()) },
                    };
                    if (std::optional<const Object&> kind = si.item_asset != 0 ? index_.find(si.item_asset) : std::nullopt)
                        if (auto k = decode(*kind))
                            si.kind_symbol = View(k->fields)[id::kItemKindSymbol].text();
                    if (si.stall != s.base.id)
                        ++model_.diagnostics.item_back_link_mismatch;
                    s.items.push_back(std::move(si));
                }
                for (const auto& item : f[id::kShopExtras].items())
                {
                    const ObjectId eid = View(item).ref();
                    if (eid == 0)
                        continue;
                    if (!taken_.insert(eid).second)
                    {
                        ++model_.diagnostics.duplicate_refs;
                        continue;
                    }
                    auto ex = fetch(eid, Role::ShopExtra);
                    if (!ex)
                    {
                        ++model_.diagnostics.unresolved_items;
                        continue;
                    }
                    const View v(ex->fields);
                    s.extras.push_back({ ex->id, live(v[id::kItemAsset]), v[id::kExtraSetting].i32() });
                }
                model_.stalls.push_back(std::move(s));
            }
        };

    } // namespace

    float Breakdown::downtime_percent() const noexcept
    {
        if (saved_downtime_percent)
            return *saved_downtime_percent;
        int sum = 0;
        for (const auto w : downtime_windows)
            sum += w;
        return static_cast<float>(sum) * 100.0f / 160.0f;
    }

    std::expected<std::vector<RideShow>, Error> load_shows(const SaveFile& file, const ObjectIndex& index)
    {
        const auto decoded = [&](const Object& o, std::string_view kind) -> std::expected<std::optional<DecodedObject>, Error> {
            if (o.class_index >= file.classes.size() || file.classes[o.class_index].name != kind)
                return std::nullopt;
            return decode_object(file, o);
        };
        std::vector<RideShow> shows;
        for (const Object& o : file.objects)
        {
            auto show = decoded(o, id::kShow);
            if (!show)
                return std::unexpected(show.error());
            if (!*show)
                continue;
            const View f((*show)->fields);
            RideShow out { .ride = f[id::kShowRide].ref(), .tricks = { }, .order = { } };
            // The show's tricks are its known tricks, by id.
            std::vector<ObjectId> known;
            for (const Value& item : f[id::kShowTricks].items())
            {
                const auto trick = index.find(View(item).ref());
                if (!trick)
                    continue;
                auto t = decoded(*trick, id::kTrick);
                if (!t)
                    return std::unexpected(t.error());
                if (!*t)
                    continue;
                const View tf((*t)->fields);
                known.push_back(View(item).ref());
                out.tricks.push_back({ .name = tf[id::kTrickName].text(), .seconds = std::max(tf[id::kTrickSeconds].number(), 0.0f), .hidden = tf[id::kTrickHidden].boolean(), .available = tf[id::kTrickAvailable].boolean(), .invented = tf[id::kTrickInvented].boolean() });
            }
            for (const Value& item : f[id::kShowOrder].items())
                if (const auto at = std::ranges::find(known, View(item).ref()); at != known.end())
                    out.order.push_back(static_cast<std::uint32_t>(at - known.begin()));
            if (out.ride != 0 && !out.tricks.empty())
                shows.push_back(std::move(out));
        }
        return shows;
    }

    std::expected<RideModel, Error> load_rides(const SaveFile& file, const ObjectIndex& index)
    {
        return Loader(file, index).run();
    }

} // namespace rct3::save
